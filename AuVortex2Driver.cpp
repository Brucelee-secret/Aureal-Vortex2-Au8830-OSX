/*
 * AuVortex2Driver.cpp
 * IOAudio device + engine for Aureal Vortex 2 (AU8830), OS X 10.6.8
 *
 * Hardware programming follows Linux sound/pci/au88x0 (au88x0_core.c / au88x0_pcm.c).
 */

#include "AuVortex2Driver.h"
#include "au8830.h"
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <libkern/OSByteOrder.h>

#define super IOAudioDevice
OSDefineMetaClassAndStructors(AuVortex2Driver, IOAudioDevice)
OSDefineMetaClassAndStructors(AuVortex2AudioEngine, IOAudioEngine)

#define AC97_RESET          0x00
#define AC97_MASTER         0x02
#define AC97_HEADPHONE      0x04
#define AC97_MASTER_MONO    0x06
#define AC97_PC_BEEP        0x0a
#define AC97_PHONE          0x0c
#define AC97_MIC            0x0e
#define AC97_LINE           0x10
#define AC97_CD             0x12
#define AC97_VIDEO          0x14
#define AC97_AUX            0x16
#define AC97_PCM            0x18
#define AC97_REC_SEL        0x1a
#define AC97_REC_GAIN       0x1c
#define AC97_GP             0x20
#define AC97_POWERDOWN      0x26
#define AC97_EXTENDED_ID    0x28
#define AC97_EXTENDED_STATUS 0x2a
#define AC97_SURROUND_MASTER 0x38
#define AC97_VENDOR_ID1     0x7c
#define AC97_VENDOR_ID2     0x7e
#define AC97_EA_SDAC        0x0080
#define AC97_EA_CDAC        0x0040
#define kAuV2VolumeMin      0
#define kAuV2VolumeMax      65535
#define kAuV2VolumeDefault  32768

#define VORTEX_CODEC_ID_SHIFT  24
#define VORTEX_CODEC_WRITE     0x00800000
#define VORTEX_CODEC_ADDSHIFT  16
#define VORTEX_CODEC_ADDMASK   0x7f0000
#define VORTEX_CODEC_DATSHIFT  0
#define VORTEX_CODEC_DATMASK   0xffff
#define CODEC_POLL_COUNT       1000

#define SRC_RATIO(x, y)        (((((x) << 15) / (y)) + 1) / 2)

#define LOG(fmt, args...) IOLog("AuVortex2: " fmt "\n", ##args)

/* ---------------- register access ---------------- */

UInt32 AuVortex2Driver::readRegister(UInt32 offset)
{
    return OSReadLittleInt32(fBaseAddress, offset);
}

void AuVortex2Driver::writeRegister(UInt32 offset, UInt32 value)
{
    OSWriteLittleInt32(fBaseAddress, offset, value);
    (void)OSReadLittleInt32(fBaseAddress, offset);
}

/* ---------------- IOAudioDevice ---------------- */

bool AuVortex2Driver::init(OSDictionary *properties)
{
    if (!super::init(properties))
        return false;

    fPCIDevice = NULL;
    fMMIO = NULL;
    fBaseAddress = NULL;
    fTimerSource = NULL;
    fAudioEngine = NULL;
    fDeviceInitialized = false;
    fVendorID = 0;
    fDeviceID = 0;
    fDmaCtrl = 0;
    fFifoStatus = kAuV2FifoStop;
    fFifoEnabled = 0;
    fCapDmaCtrl = 0;
    fCapFifoStatus = kAuV2FifoStop;
    fCapFifoEnabled = 0;
    fPeriodReal = 0;
    fPeriodVirt = 0;
    fPendingIrq = 0;
    fMasterVolume = kAuV2VolumeDefault;
    fMuted = false;
    fInputVolume = kAuV2VolumeDefault;
    fInputMuted = false;
    bzero(fMixChannels, sizeof(fMixChannels));
    return true;
}

void AuVortex2Driver::free(void)
{
    if (fTimerSource) {
        fTimerSource->disable();
        if (getWorkLoop())
            getWorkLoop()->removeEventSource(fTimerSource);
        fTimerSource->release();
        fTimerSource = NULL;
    }

    if (fMMIO) {
        fMMIO->release();
        fMMIO = NULL;
        fBaseAddress = NULL;
    }

    if (fPCIDevice) {
        fPCIDevice->close(this);
        fPCIDevice->release();
        fPCIDevice = NULL;
    }

    fAudioEngine = NULL;
    super::free();
}

IOService *AuVortex2Driver::probe(IOService *provider, SInt32 *score)
{
    IOPCIDevice *pci = OSDynamicCast(IOPCIDevice, provider);
    if (!pci)
        return NULL;

    fVendorID = pci->configRead16(kIOPCIConfigVendorID);
    fDeviceID = pci->configRead16(kIOPCIConfigDeviceID);
    LOG("probe vendor=0x%04x device=0x%04x", fVendorID, fDeviceID);

    if (fVendorID != 0x12EB)
        return NULL;
    if (fDeviceID != 0x0002 && fDeviceID != 0x0001 && fDeviceID != 0x0003)
        return NULL;

    *score = 10000;
    return this;
}

bool AuVortex2Driver::initHardware(IOService *provider)
{
    if (!super::initHardware(provider))
        return false;

    setDeviceName("Aureal Vortex 2");
    setDeviceShortName("Au8830");
    setManufacturerName("Aureal");
    setDeviceTransportType(kIOAudioDeviceTransportTypePCI);

    fPCIDevice = OSDynamicCast(IOPCIDevice, provider);
    if (!fPCIDevice)
        return false;
    fPCIDevice->retain();

    if (!fPCIDevice->open(this)) {
        LOG("PCI open failed");
        return false;
    }

    if (!configurePCIDevice() || !mapHardwareRegisters())
        return false;

    if (!initializeVortexHardware())
        return false;

    if (!setupTimer())
        LOG("warning: timer not attached");

    fAudioEngine = new AuVortex2AudioEngine;
    if (!fAudioEngine) {
        LOG("engine alloc failed");
        return false;
    }
    if (!fAudioEngine->init(this)) {
        LOG("engine init failed");
        fAudioEngine->release();
        fAudioEngine = NULL;
        return false;
    }

    /* IOAudioDevice::activateAudioEngine() calls engine initHardware(). Do not call it twice. */
    if (activateAudioEngine(fAudioEngine) != kIOReturnSuccess) {
        LOG("activateAudioEngine failed");
        fAudioEngine->release();
        fAudioEngine = NULL;
        return false;
    }

    fAudioEngine->release();
    fDeviceInitialized = true;
    registerService();
    LOG("hardware ready");
    return true;
}

void AuVortex2Driver::stop(IOService *provider)
{
    if (fDeviceInitialized) {
        shutdownHardware();
        fDeviceInitialized = false;
    }
    super::stop(provider);
}

bool AuVortex2Driver::configurePCIDevice(void)
{
    fPCIDevice->setMemoryEnable(true);
    fPCIDevice->setIOEnable(true);
    fPCIDevice->setBusMasterEnable(true);
    return true;
}

bool AuVortex2Driver::mapHardwareRegisters(void)
{
    fMMIO = fPCIDevice->mapDeviceMemoryWithRegister(kIOPCIConfigBaseAddress0);
    if (!fMMIO) {
        LOG("BAR0 map failed");
        return false;
    }
    fBaseAddress = (volatile UInt8 *)fMMIO->getVirtualAddress();
    LOG("MMIO %p size %lu", fBaseAddress, (unsigned long)fMMIO->getLength());
    return fBaseAddress != NULL;
}

/* ---------------- mixer ---------------- */

void AuVortex2Driver::mixerEnSr(int channel)
{
    writeRegister(VORTEX_MIXER_SR, readRegister(VORTEX_MIXER_SR) | (1 << channel));
}

void AuVortex2Driver::mixSetOutputVol(UInt8 mix, UInt8 vol)
{
    writeRegister(VORTEX_MIX_VOL_A + (mix << 2), vol);
    UInt32 temp = readRegister(VORTEX_MIX_VOL_B + (mix << 2));
    if ((temp != 0x80) || (vol == 0x80))
        return;
    writeRegister(VORTEX_MIX_VOL_B + (mix << 2), vol);
}

void AuVortex2Driver::mixSetInputVol(UInt8 mix, UInt8 mixin, UInt8 vol)
{
    writeRegister(VORTEX_MIX_INVOL_A + (((mix << 5) + mixin) << 2), vol);
    UInt32 temp = readRegister(VORTEX_MIX_INVOL_B + (((mix << 5) + mixin) << 2));
    if ((temp != 0x80) || (vol == 0x80))
        return;
    writeRegister(VORTEX_MIX_INVOL_B + (((mix << 5) + mixin) << 2), vol);
}

void AuVortex2Driver::mixSetEnableBit(UInt8 mix, int mixin, int en)
{
    int addr = mixin;
    if (mixin < 0)
        addr = mixin + 3;
    addr = ((mix << 3) + (addr >> 2)) << 2;
    UInt32 temp = readRegister(VORTEX_MIX_ENIN + addr);
    if (en)
        temp |= (1 << (mixin & 3));
    else
        temp &= ~(1 << (mixin & 3));
    writeRegister(VORTEX_MIX_INVOL_B + (((mix << 5) + mixin) << 2), 0x80);
    writeRegister(VORTEX_MIX_SMP + (mixin << 2), 0);
    writeRegister(VORTEX_MIX_SMP + 4 + (mixin << 2), 0);
    writeRegister(VORTEX_MIX_ENIN + addr, temp);
}

void AuVortex2Driver::mixEnableInput(UInt8 mix, int mixin)
{
    mixSetInputVol(mix, mixin, 0x80);
    mixSetEnableBit(mix, mixin, 0);
    if ((fMixChannels[mix] & (1 << mixin)) == 0) {
        mixSetInputVol(mix, mixin, 0x80);
        fMixChannels[mix] |= (1 << mixin);
    }
    mixSetEnableBit(mix, mixin, 1);
}

void AuVortex2Driver::mixDisableInput(UInt8 mix, int mixin)
{
    mixSetInputVol(mix, mixin, 0x80);
    fMixChannels[mix] &= ~(1 << mixin);
    mixSetEnableBit(mix, mixin, 0);
}

void AuVortex2Driver::mixerAddWTD(UInt8 mix, UInt8 ch)
{
    UInt32 temp = readRegister(VORTEX_MIXER_SR);
    if ((temp & (1 << ch)) == 0) {
        writeRegister(VORTEX_MIXER_CHNBASE + (ch << 2), mix);
        mixerEnSr(ch);
        return;
    }
    UInt32 prev = VORTEX_MIXER_CHNBASE + (ch << 2);
    temp = readRegister(prev);
    int lifeboat = 0;
    while (temp & 0x10) {
        prev = VORTEX_MIXER_RTBASE + ((temp & 0xf) << 2);
        temp = readRegister(prev);
        if (++lifeboat > 0xf)
            return;
    }
    writeRegister(VORTEX_MIXER_RTBASE + ((temp & 0xf) << 2), mix);
    writeRegister(prev, (temp & 0xf) | 0x10);
}

void AuVortex2Driver::mixerInit(void)
{
    UInt32 addr;
    int x;

    bzero(fMixChannels, sizeof(fMixChannels));

    addr = VORTEX_MIX_SMP + 0x17c;
    for (x = 0x5f; x >= 0; x--) {
        writeRegister(addr, 0);
        addr -= 4;
    }
    addr = VORTEX_MIX_ENIN + 0x1fc;
    for (x = 0x7f; x >= 0; x--) {
        writeRegister(addr, 0);
        addr -= 4;
    }
    addr = VORTEX_MIX_SMP + 0x17c;
    for (x = 0x5f; x >= 0; x--) {
        writeRegister(addr, 0);
        addr -= 4;
    }
    addr = VORTEX_MIX_INVOL_A + 0x7fc;
    for (x = 0x1ff; x >= 0; x--) {
        writeRegister(addr, 0x80);
        addr -= 4;
    }
    addr = VORTEX_MIX_VOL_A + 0x3c;
    for (x = 0xf; x >= 0; x--) {
        writeRegister(addr, 0x80);
        addr -= 4;
    }
    addr = VORTEX_MIX_INVOL_B + 0x7fc;
    for (x = 0x1ff; x >= 0; x--) {
        writeRegister(addr, 0x80);
        addr -= 4;
    }
    addr = VORTEX_MIX_VOL_B + 0x3c;
    for (x = 0xf; x >= 0; x--) {
        writeRegister(addr, 0x80);
        addr -= 4;
    }
    addr = VORTEX_MIXER_RTBASE + (MIXER_RTBASE_SIZE - 1) * 4;
    for (x = (MIXER_RTBASE_SIZE - 1); x >= 0; x--) {
        writeRegister(addr, 0);
        addr -= 4;
    }
    writeRegister(VORTEX_MIXER_SR, 0);
}

/* ---------------- FIFO ---------------- */

void AuVortex2Driver::fifoClearAdbData(int fifo)
{
    int x;
    for (x = FIFO_SIZE - 1; x >= 0; x--)
        writeRegister(VORTEX_FIFO_ADBDATA + (((fifo << FIFO_SIZE_BITS) + x) << 2), 0);
}

void AuVortex2Driver::fifoSetAdbValid(int fifo, int en)
{
    writeRegister(VORTEX_FIFO_ADBCTRL + (fifo << 2),
                  (readRegister(VORTEX_FIFO_ADBCTRL + (fifo << 2)) & 0xffffffef) |
                  ((en & 1) << 4) | FIFO_U1);
}

void AuVortex2Driver::fifoSetAdbCtrl(int fifo, int stereo, int priority,
                                     int empty, int valid, int f)
{
    int lifeboat = 0;
    int this_4 = 0x2;
    UInt32 temp;

    do {
        temp = readRegister(VORTEX_FIFO_ADBCTRL + (fifo << 2));
        if (lifeboat++ > 0xbb8) {
            LOG("fifo_setadbctrl fail");
            break;
        }
    } while (temp & FIFO_RDONLY);

    if (valid) {
        if ((temp & FIFO_VALID) == 0) {
            fifoClearAdbData(fifo);
            temp = (this_4 & 0x3f) << 0xc;
            temp = (temp & 0xfffffffd) | ((stereo & 1) << 1);
            temp = (temp & 0xfffffff3) | ((priority & 3) << 2);
            temp = (temp & 0xffffffef) | ((valid & 1) << 4);
            temp |= FIFO_U1;
            temp = (temp & 0xffffffdf) | ((empty & 1) << 5);
            /* CHIP_AU8830 */
            temp = (temp & 0xf7ffffff) | ((f & 1) << 0x1b);
            temp = (temp & 0xefffffff) | ((f & 1) << 0x1c);
        }
    } else {
        if (temp & FIFO_VALID) {
            temp = ((f & 1) << 0x1b) | (temp & 0xe7ffffef) | FIFO_BITS;
        } else {
            fifoClearAdbData(fifo);
        }
    }
    writeRegister(VORTEX_FIFO_ADBCTRL + (fifo << 2), temp);
    (void)readRegister(VORTEX_FIFO_ADBCTRL + (fifo << 2));
}

void AuVortex2Driver::fifoInit(void)
{
    int x;
    UInt32 addr = VORTEX_FIFO_ADBCTRL + ((NR_ADB - 1) * 4);
    for (x = NR_ADB - 1; x >= 0; x--) {
        writeRegister(addr, FIFO_U0 | FIFO_U1);
        fifoClearAdbData(x);
        addr -= 4;
    }
    /* AU8830 GIRT trigger */
    writeRegister(0x17000, 0x61);
    writeRegister(0x17004, 0x61);
    writeRegister(0x17008, 0x61);
}

/* ---------------- ADB routes ---------------- */

void AuVortex2Driver::adbInit(void)
{
    int i;
    writeRegister(VORTEX_ADB_SR, 0);
    for (i = 0; i < VORTEX_ADB_RTBASE_COUNT; i++)
        writeRegister(VORTEX_ADB_RTBASE + (i << 2),
                      readRegister(VORTEX_ADB_RTBASE + (i << 2)) | ROUTE_MASK);
    for (i = 0; i < VORTEX_ADB_CHNBASE_COUNT; i++)
        writeRegister(VORTEX_ADB_CHNBASE + (i << 2),
                      readRegister(VORTEX_ADB_CHNBASE + (i << 2)) | ROUTE_MASK);
}

void AuVortex2Driver::adbEnSr(int channel)
{
    writeRegister(VORTEX_ADB_SR, readRegister(VORTEX_ADB_SR) | (1 << channel));
}

void AuVortex2Driver::adbAddRoute(UInt8 channel, UInt32 route)
{
    writeRegister(VORTEX_ADB_RTBASE + ((route & ADB_MASK) << 2), ROUTE_MASK);

    UInt32 temp = readRegister(VORTEX_ADB_CHNBASE + (channel << 2)) & ADB_MASK;
    if (temp == ADB_MASK) {
        writeRegister(VORTEX_ADB_CHNBASE + (channel << 2), route);
        adbEnSr(channel);
        return;
    }

    int lifeboat = 0;
    UInt32 prev;
    do {
        prev = temp;
        temp = readRegister(VORTEX_ADB_RTBASE + (temp << 2)) & ADB_MASK;
        if (++lifeboat > ADB_MASK)
            return;
    } while (temp != ADB_MASK);
    writeRegister(VORTEX_ADB_RTBASE + (prev << 2), route);
}

void AuVortex2Driver::adbRoute(int en, UInt8 channel, UInt8 source, UInt8 dest)
{
    UInt32 route = ((source & ADB_MASK) << ADB_SHIFT) | (dest & ADB_MASK);
    if (!en)
        return;

    adbAddRoute(channel, route);

    if ((source < (OFFSET_SRCOUT + NR_SRC)) && (source >= OFFSET_SRCOUT))
        srcAddWTD((UInt8)(source - OFFSET_SRCOUT), channel);
    else if ((source < (OFFSET_MIXOUT + NR_MIXOUT)) && (source >= OFFSET_MIXOUT))
        mixerAddWTD((UInt8)(source - OFFSET_MIXOUT), channel);
}

/* ---------------- SRC ---------------- */

void AuVortex2Driver::srcEnSr(int channel)
{
    writeRegister(VORTEX_SRCBLOCK_SR, readRegister(VORTEX_SRCBLOCK_SR) | (1 << channel));
}

void AuVortex2Driver::srcAddWTD(UInt8 src, UInt8 ch)
{
    UInt32 temp = readRegister(VORTEX_SRCBLOCK_SR);
    if ((temp & (1 << ch)) == 0) {
        writeRegister(VORTEX_SRC_CHNBASE + (ch << 2), src);
        srcEnSr(ch);
        return;
    }
    UInt32 prev = VORTEX_SRC_CHNBASE + (ch << 2);
    temp = readRegister(prev);
    int lifeboat = 0;
    while (temp & 0x10) {
        prev = VORTEX_SRC_RTBASE + ((temp & 0xf) << 2);
        temp = readRegister(prev);
        if (++lifeboat > 0xf)
            return;
    }
    writeRegister(VORTEX_SRC_RTBASE + ((temp & 0xf) << 2), src);
    writeRegister(prev, (temp & 0xf) | 0x10);
}

void AuVortex2Driver::srcFlush(UInt8 src)
{
    int i;
    for (i = 0x1f; i >= 0; i--)
        writeRegister(VORTEX_SRC_DATA0 + (src << 7) + (i << 2), 0);
    writeRegister(VORTEX_SRC_DATA + (src << 3), 0);
    writeRegister(VORTEX_SRC_DATA + (src << 3) + 4, 0);
}

void AuVortex2Driver::srcClearDrift(UInt8 src)
{
    writeRegister(VORTEX_SRC_DRIFT0 + (src << 2), 0);
    writeRegister(VORTEX_SRC_DRIFT1 + (src << 2), 0);
    writeRegister(VORTEX_SRC_DRIFT2 + (src << 2), 1);
}

void AuVortex2Driver::srcSetThrottle(UInt8 src, int en)
{
    UInt32 temp = readRegister(VORTEX_SRC_SOURCE);
    if (en)
        temp |= (1 << src);
    else
        temp &= ~(1 << src);
    writeRegister(VORTEX_SRC_SOURCE, temp);
}

void AuVortex2Driver::srcPersistRatio(UInt8 src, UInt32 ratio)
{
    int lifeboat = 0;
    UInt32 temp;
    do {
        writeRegister(VORTEX_SRC_CONVRATIO + (src << 2), ratio);
        temp = readRegister(VORTEX_SRC_CONVRATIO + (src << 2));
        if (++lifeboat > 9)
            break;
    } while (temp != ratio);
}

void AuVortex2Driver::srcSetup(UInt8 src, UInt32 cr, int d, int dirplay)
{
    int esi, ebp = 0, esp10;
    UInt32 tr;

    srcFlush(src);

    if ((cr & 0x10000) && (cr != 0x10000)) {
        tr = 0;
        esi = 0x11 - ((cr >> 0xe) & 7);
        if (cr & 0x3fff)
            esi -= 1;
        else
            esi -= 2;
    } else {
        tr = 1;
        esi = 0xc;
    }

    srcClearDrift(src);
    srcSetThrottle(src, dirplay);

    if ((dirplay == 0)) {
        esp10 = tr ? 0xf : 0xc;
        ebp = 0;
    } else {
        ebp = tr ? 0xf : 0xc;
        esp10 = 0;
    }

    writeRegister(VORTEX_SRC_U0 + (src << 2),
                  (1 << 0x9) | ((esi & 0xf) << 4) | d);
    srcPersistRatio(src, cr);
    writeRegister(VORTEX_SRC_U1 + (src << 2), 0);
    writeRegister(VORTEX_SRC_U2 + (src << 2),
                  (tr << 0x11) | (dirplay << 0x10) | (ebp << 0x8) | esp10);
}

void AuVortex2Driver::srcInit(void)
{
    UInt32 addr;
    int x;
    writeRegister(VORTEX_SRC_SOURCESIZE, 0x1ff);
    addr = VORTEX_SRC_RTBASE + 0x3c;
    for (x = 0xf; x >= 0; x--) {
        writeRegister(addr, 0);
        addr -= 4;
    }
    addr = VORTEX_SRC_CHNBASE + 0x54;
    for (x = 0x15; x >= 0; x--) {
        writeRegister(addr, 0);
        addr -= 4;
    }
}

void AuVortex2Driver::srcSet48kPlayback(void)
{
    UInt32 cvrt = SRC_RATIO(kAuV2SampleRate, 48000);
    srcSetup(kAuV2SrcLeft, cvrt, kAuV2SrcLeft, 1);
    srcSetup(kAuV2SrcRight, cvrt, kAuV2SrcRight, 1);
}

void AuVortex2Driver::srcSet48kCapture(void)
{
    UInt32 cvrt = SRC_RATIO(48000, kAuV2SampleRate);
    srcSetup(kAuV2CapSrcLeft, cvrt, kAuV2CapSrcLeft, 0);
    srcSetup(kAuV2CapSrcRight, cvrt, kAuV2CapSrcRight, 0);
}

void AuVortex2Driver::adbRouteLRT(int en, UInt8 ch, UInt8 source0, UInt8 source1, UInt8 dest)
{
    UInt8 dest1 = dest;
    if (dest < 0x10)
        dest1 = (UInt8)(dest + 0x20);
    if (!en)
        return;
    adbRoute(en, ch, source0, dest);
    adbRoute(en, ch, source1, dest1);
}

/* ---------------- connections ---------------- */

void AuVortex2Driver::connectionMixinMix(int en, UInt8 mixin, UInt8 mix)
{
    if (en) {
        mixEnableInput(mix, mixin);
        mixSetInputVol(mix, mixin, MIX_DEFIGAIN);
    } else {
        mixDisableInput(mix, mixin);
    }
}

void AuVortex2Driver::connectionMixAdb(int en, UInt8 ch, UInt8 mix, UInt8 dest)
{
    adbRoute(en, ch, ADB_MIXOUT(mix), dest);
    mixSetOutputVol(mix, MIX_DEFOGAIN);
}

void AuVortex2Driver::connectAnalogPlayback(int en)
{
    /* Направляем чистый стереосигнал и на первый ЦАП (слоты 0/1) и на второй ЦАП (слоты 4/5) */
    connectionMixAdb(en, kAuV2ADBChannel, kAuV2MixLeft, ADB_CODECOUT(0));
    connectionMixAdb(en, kAuV2ADBChannel, kAuV2MixRight, ADB_CODECOUT(1));
    connectionMixAdb(en, kAuV2ADBChannel, kAuV2MixRearLeft, ADB_CODECOUT(4));
    connectionMixAdb(en, kAuV2ADBChannel, kAuV2MixRearRight, ADB_CODECOUT(5));

    adbRoute(en, kAuV2DmaRouteCh, ADB_DMA(kAuV2DMAChannel), ADB_SRCIN(kAuV2SrcLeft));
    adbRoute(en, kAuV2DmaRouteCh, ADB_DMA(kAuV2DMAChannel), ADB_SRCIN(kAuV2SrcRight));
    adbRoute(en, kAuV2ADBChannel, ADB_SRCOUT(kAuV2SrcLeft), ADB_MIXIN(kAuV2MixinLeft));
    adbRoute(en, kAuV2ADBChannel, ADB_SRCOUT(kAuV2SrcRight), ADB_MIXIN(kAuV2MixinRight));

    connectionMixinMix(en, kAuV2MixinLeft, kAuV2MixLeft);
    connectionMixinMix(en, kAuV2MixinRight, kAuV2MixRight);
    connectionMixinMix(en, kAuV2MixinLeft, kAuV2MixRearLeft);
    connectionMixinMix(en, kAuV2MixinRight, kAuV2MixRearRight);
}

void AuVortex2Driver::connectAnalogCapture(int en)
{
    /* AC97 ADC stereo -> mixin 2/3 (Linux mixcapt). */
    adbRoute(en, kAuV2ADBChannel, ADB_CODECIN(0), ADB_MIXIN(kAuV2CapMixinLeft));
    adbRoute(en, kAuV2ADBChannel, ADB_CODECIN(1), ADB_MIXIN(kAuV2CapMixinRight));

    connectionMixinMix(en, kAuV2CapMixinLeft, kAuV2CapMixLeft);
    connectionMixinMix(en, kAuV2CapMixinRight, kAuV2CapMixRight);

    adbRoute(en, kAuV2ADBChannel, ADB_MIXOUT(kAuV2CapMixLeft), ADB_SRCIN(kAuV2CapSrcLeft));
    mixSetOutputVol(kAuV2CapMixLeft, MIX_DEFOGAIN);
    adbRoute(en, kAuV2ADBChannel, ADB_MIXOUT(kAuV2CapMixRight), ADB_SRCIN(kAuV2CapSrcRight));
    mixSetOutputVol(kAuV2CapMixRight, MIX_DEFOGAIN);

    /* Both SRC outputs into DMA1 (Linux vortex_routeLRT / fifo A). */
    adbRouteLRT(en, kAuV2CapRouteCh,
                ADB_SRCOUT(kAuV2CapSrcLeft),
                ADB_SRCOUT(kAuV2CapSrcRight),
                ADB_DMA(kAuV2CapDMAChannel));
}

void AuVortex2Driver::connectSpdifPlayback(int en)
{
    /* Copy analog PCM mixins onto dedicated SPDIF mixouts (Linux mixspdif). */
    connectionMixinMix(en, kAuV2MixinLeft, kAuV2SpdifMixLeft);
    connectionMixinMix(en, kAuV2MixinRight, kAuV2SpdifMixRight);
    connectionMixAdb(en, kAuV2SpdifChannel, kAuV2SpdifMixLeft, ADB_SPDIFOUT(0));
    connectionMixAdb(en, kAuV2SpdifChannel, kAuV2SpdifMixRight, ADB_SPDIFOUT(1));
}

void AuVortex2Driver::spdifInit(int rate)
{
    int i;
    UInt32 this_38 = 0;
    UInt32 spdif_sr = (UInt32)rate;

    writeRegister(VORTEX_SPDIF_FLAGS, readRegister(VORTEX_SPDIF_FLAGS) & 0xfff3fffd);
    for (i = 0; i < 11; i++)
        writeRegister(VORTEX_SPDIF_CFG1 + (i << 2), 0);
    writeRegister(VORTEX_CODEC_EN, readRegister(VORTEX_CODEC_EN) | EN_SPDIF);

    spdif_sr |= 0x8c;
    if (rate == 48000) {
        this_38 = 0x02000000;
        spdif_sr |= 2;
        spdif_sr &= ~1U;
    }
    writeRegister(VORTEX_SPDIF_CFG0, this_38 & 0xffff);
    writeRegister(VORTEX_SPDIF_CFG1, this_38 >> 0x10);
    writeRegister(VORTEX_SPDIF_SMPRATE, spdif_sr);
    LOG("SPDIF init rate=%d cfg0=0x%04x smprate=0x%08x", rate,
        (unsigned)(this_38 & 0xffff), (unsigned)spdif_sr);
        
}

/* ---------------- AC97 ---------------- */

void AuVortex2Driver::codecWrite(UInt8 addr, UInt16 data)
{
    unsigned int lifeboat = 0;
    while (!(readRegister(VORTEX_CODEC_CTRL) & 0x100)) {
        IODelay(100);
        if (lifeboat++ > CODEC_POLL_COUNT) {
            LOG("ac97 codec stuck busy (write)");
            return;
        }
    }
    writeRegister(VORTEX_CODEC_IO,
                  ((addr << VORTEX_CODEC_ADDSHIFT) & VORTEX_CODEC_ADDMASK) |
                  ((data << VORTEX_CODEC_DATSHIFT) & VORTEX_CODEC_DATMASK) |
                  VORTEX_CODEC_WRITE);
    (void)readRegister(VORTEX_CODEC_IO);
}

UInt16 AuVortex2Driver::codecRead(UInt8 addr)
{
    unsigned int lifeboat = 0;
    UInt32 data;
    UInt32 read_addr;

    while (!(readRegister(VORTEX_CODEC_CTRL) & 0x100)) {
        IODelay(100);
        if (lifeboat++ > CODEC_POLL_COUNT) {
            LOG("ac97 codec stuck busy (read)");
            return 0xffff;
        }
    }

    read_addr = ((addr << VORTEX_CODEC_ADDSHIFT) & VORTEX_CODEC_ADDMASK);
    writeRegister(VORTEX_CODEC_IO, read_addr);

    lifeboat = 0;
    do {
        IODelay(100);
        data = readRegister(VORTEX_CODEC_IO);
        if (lifeboat++ > CODEC_POLL_COUNT) {
            LOG("ac97 address never arrived");
            return 0xffff;
        }
    } while ((data & VORTEX_CODEC_ADDMASK) != (addr << VORTEX_CODEC_ADDSHIFT));

    return (UInt16)(data & VORTEX_CODEC_DATMASK);
}

void AuVortex2Driver::codecInit(void)
{
    int i;
    for (i = 0; i < 32; i++) {
        writeRegister(VORTEX_CODEC_CHN + (i << 2), (UInt32)(-i));
        IOSleep(2);
    }
    writeRegister(VORTEX_CODEC_CTRL, 0x00a8);
    IOSleep(2);
    writeRegister(VORTEX_CODEC_CTRL, 0x80a8);
    IOSleep(2);
    writeRegister(VORTEX_CODEC_CTRL, 0x80e8);
    IOSleep(2);
    writeRegister(VORTEX_CODEC_CTRL, 0x80a8);
    IOSleep(2);
    writeRegister(VORTEX_CODEC_CTRL, 0x00a8);
    IOSleep(2);
    writeRegister(VORTEX_CODEC_CTRL, 0x00e8);
    for (i = 0; i < 32; i++) {
        writeRegister(VORTEX_CODEC_CHN + (i << 2), (UInt32)(-i));
        IOSleep(5);
    }
    writeRegister(VORTEX_CODEC_CTRL, 0xe8);
    IOSleep(1);
    writeRegister(VORTEX_CODEC_EN, readRegister(VORTEX_CODEC_EN) | EN_CODEC);
}

bool AuVortex2Driver::initAC97(void)
{
    codecWrite(AC97_RESET, 0); IOSleep(20);
    codecWrite(AC97_POWERDOWN, 0x0000); codecWrite(AC97_MASTER_MONO, 0x8000);
    codecWrite(AC97_PC_BEEP, 0x8000); codecWrite(AC97_PHONE, 0x8008);
    codecWrite(AC97_MIC, 0x8008); codecWrite(AC97_LINE, 0x8808);
    codecWrite(AC97_CD, 0x8808); codecWrite(AC97_VIDEO, 0x8808);
    codecWrite(AC97_AUX, 0x8808); codecWrite(AC97_REC_SEL, 0x0000);
    codecWrite(AC97_REC_GAIN, 0x8000);

    codecWrite(0x5e, 0xabba); // Будим дополнительный ЦАП

    // --- ВКЛЮЧАЕМ 3D SURROUND НА ГНЕЗДО 1 ---
    // Регистр 0x20 (GP) включает микширование 3D-процессора на главные каналы (бит 0x2000 или 0x4000)
    codecWrite(0x20, codecRead(0x20) | 0x6000); 
    // Регистр 0x22 контролирует глубину эффекта. Выставим умеренно-максимальный 3D-объем (0x000f)
    codecWrite(0x22, 0x000f); 
    // ----------------------------------------

    codecWrite(AC97_SURROUND_MASTER, 0x0000);

    {
        UInt16 eid = codecRead(AC97_EXTENDED_ID); UInt16 estat = codecRead(AC97_EXTENDED_STATUS);
        if (eid & AC97_EA_SDAC) estat |= AC97_EA_SDAC;
        if (eid & AC97_EA_CDAC) estat |= AC97_EA_CDAC;
        codecWrite(AC97_EXTENDED_STATUS, estat);
    }
    applyAnalogVolume();
    applyAnalogInputGain();
    return true;
}



void AuVortex2Driver::applyAnalogVolume(void)
{
    UInt16 att;
    UInt16 reg;

    if (!fBaseAddress)
        return;

    // ТОТАЛЬНЫЙ АППАРАТНЫЙ MUTE (100% тишина на обоих гнездах)
    if (fMuted || fMasterVolume <= 0) {
        codecWrite(AC97_MASTER, 0x801f);
        codecWrite(AC97_HEADPHONE, 0x801f);
        codecWrite(AC97_PCM, 0x801f);
        codecWrite(AC97_SURROUND_MASTER, 0x801f);
        codecWrite(0x5e, 0x0000);                  // Отключаем 3D-блок
        
        // Переводим аналоговые выходы кодека в режим сна (выключаем Vref и микшер)
        codecWrite(AC97_POWERDOWN, 0x0f00); 
        return;
    }

    // Если звук включен — возвращаем полное питание аналоговой части кодека
    codecWrite(AC97_POWERDOWN, 0x0000); 
    // Исправлено: добавлены пропущенные скобки в вызовах
    codecWrite(0x5e, 0x0000); IOSleep(1); codecWrite(0x5e, 0xabba); // Сброс 3D-блока

    att = (UInt16)(0x1f - ((fMasterVolume * 0x1f) / kAuV2VolumeMax));
    if (att > 0x1f)
        att = 0x1f;

    reg = (UInt16)((att << 8) | att);

    codecWrite(AC97_MASTER, reg);
    codecWrite(AC97_HEADPHONE, reg);
    codecWrite(AC97_PCM, 0x0000);
    codecWrite(AC97_SURROUND_MASTER, reg);
}






IOReturn AuVortex2Driver::volumeChangeHandler(OSObject *target, IOAudioControl *control,
                                              SInt32 oldValue, SInt32 newValue)
{
    AuVortex2Driver *driver = OSDynamicCast(AuVortex2Driver, target);
    if (!driver)
        return kIOReturnBadArgument;
    (void)control;
    (void)oldValue;
    driver->fMasterVolume = newValue;
    driver->applyAnalogVolume();
    return kIOReturnSuccess;
}

IOReturn AuVortex2Driver::muteChangeHandler(OSObject *target, IOAudioControl *control,
                                            SInt32 oldValue, SInt32 newValue)
{
    AuVortex2Driver *driver = OSDynamicCast(AuVortex2Driver, target);
    if (!driver)
        return kIOReturnBadArgument;
    (void)control;
    (void)oldValue;
    driver->fMuted = (newValue != 0);
    driver->applyAnalogVolume();
    return kIOReturnSuccess;
}

void AuVortex2Driver::applyAnalogInputGain(void)
{
    UInt16 att;
    UInt16 reg;

    if (!fBaseAddress)
        return;

    codecWrite(AC97_REC_SEL, 0x0000); /* Mic */

    if (fInputMuted || fInputVolume <= 0) {
        codecWrite(AC97_MIC, 0x8000);
        codecWrite(AC97_REC_GAIN, 0x8000);
        return;
    }

    att = (UInt16)(0x0f - ((fInputVolume * 0x0f) / kAuV2VolumeMax));
    if (att > 0x0f)
        att = 0x0f;
    reg = (UInt16)((att << 8) | att);
    codecWrite(AC97_MIC, 0x0040);
    codecWrite(AC97_REC_GAIN, reg);
}

IOReturn AuVortex2Driver::inputVolumeChangeHandler(OSObject *target, IOAudioControl *control,
                                                   SInt32 oldValue, SInt32 newValue)
{
    AuVortex2Driver *driver = OSDynamicCast(AuVortex2Driver, target);
    if (!driver)
        return kIOReturnBadArgument;
    (void)control;
    (void)oldValue;
    driver->fInputVolume = newValue;
    driver->applyAnalogInputGain();
    return kIOReturnSuccess;
}

IOReturn AuVortex2Driver::inputMuteChangeHandler(OSObject *target, IOAudioControl *control,
                                                 SInt32 oldValue, SInt32 newValue)
{
    AuVortex2Driver *driver = OSDynamicCast(AuVortex2Driver, target);
    if (!driver)
        return kIOReturnBadArgument;
    (void)control;
    (void)oldValue;
    driver->fInputMuted = (newValue != 0);
    driver->applyAnalogInputGain();
    return kIOReturnSuccess;
}

/* ---------------- DMA ---------------- */

void AuVortex2Driver::dmaSetMode(int ie, int dir, int fmt, int stereo)
{
    fDmaCtrl = (fDmaCtrl & ~OFFSET_MASK);
    fDmaCtrl = (fDmaCtrl & ~IE_MASK) | ((ie << IE_SHIFT) & IE_MASK);
    fDmaCtrl = (fDmaCtrl & ~DIR_MASK) | ((dir << DIR_SHIFT) & DIR_MASK);
    fDmaCtrl = (fDmaCtrl & ~FMT_MASK) | ((fmt << FMT_SHIFT) & FMT_MASK);
    writeRegister(VORTEX_ADBDMA_CTRL + (kAuV2DMAChannel << 2), fDmaCtrl);
    (void)readRegister(VORTEX_ADBDMA_CTRL + (kAuV2DMAChannel << 2));
    (void)stereo;
}

void AuVortex2Driver::dmaSetBuffers(IOPhysicalAddress pagePhys[kAuV2NumPeriods])
{
    UInt32 psize = kAuV2PeriodBytes;
    UInt32 cfg0 = 0;
    UInt32 cfg1 = 0;

    /* 4 pages — same switch/fallthrough as Linux vortex_adbdma_setbuffers */
    cfg1 |= 0x88000000 | 0x44000000 | 0x30000000 | (psize - 1);
    writeRegister(VORTEX_ADBDMA_BUFBASE + (kAuV2DMAChannel << 4) + 0xc, (UInt32)pagePhys[3]);
    cfg0 |= 0x12000000;
    cfg1 |= 0x80000000 | 0x40000000 | ((psize - 1) << 0xc);
    writeRegister(VORTEX_ADBDMA_BUFBASE + (kAuV2DMAChannel << 4) + 0x8, (UInt32)pagePhys[2]);
    cfg0 |= 0x88000000 | 0x44000000 | 0x10000000 | (psize - 1);
    writeRegister(VORTEX_ADBDMA_BUFBASE + (kAuV2DMAChannel << 4) + 0x4, (UInt32)pagePhys[1]);
    cfg0 |= 0x80000000 | 0x40000000 | ((psize - 1) << 0xc);
    writeRegister(VORTEX_ADBDMA_BUFBASE + (kAuV2DMAChannel << 4), (UInt32)pagePhys[0]);

    writeRegister(VORTEX_ADBDMA_BUFCFG0 + (kAuV2DMAChannel << 3), cfg0);
    writeRegister(VORTEX_ADBDMA_BUFCFG1 + (kAuV2DMAChannel << 3), cfg1);

    writeRegister(VORTEX_ADBDMA_CTRL + (kAuV2DMAChannel << 2), fDmaCtrl);
    writeRegister(VORTEX_ADBDMA_START + (kAuV2DMAChannel << 2),
                  0 << ((0xf - (kAuV2DMAChannel & 0xf)) * 2));
    fPeriodReal = 0;
    fPeriodVirt = 0;
}

void AuVortex2Driver::dmaStartFifo(void)
{
    int empty = 0;
    int priority = 0;

    switch (fFifoStatus) {
    case kAuV2FifoStart:
        fifoSetAdbValid(kAuV2DMAChannel, fFifoEnabled ? 1 : 0);
        break;
    case kAuV2FifoStop:
        empty = 1;
        writeRegister(VORTEX_ADBDMA_CTRL + (kAuV2DMAChannel << 2), fDmaCtrl);
        fifoSetAdbCtrl(kAuV2DMAChannel, 1, priority, empty, fFifoEnabled ? 1 : 0, 0);
        break;
    case kAuV2FifoPause:
        fifoSetAdbCtrl(kAuV2DMAChannel, 1, priority, empty, fFifoEnabled ? 1 : 0, 0);
        break;
    }
    fFifoStatus = kAuV2FifoStart;
}

void AuVortex2Driver::dmaStopFifo(void)
{
    if (fFifoStatus == kAuV2FifoStart)
        fifoSetAdbCtrl(kAuV2DMAChannel, 1, 0, 0, 0, 0);
    else if (fFifoStatus == kAuV2FifoStop)
        return;
    fFifoStatus = kAuV2FifoStop;
    fFifoEnabled = 0;
}

void AuVortex2Driver::programPlaybackDMA(IOPhysicalAddress pagePhys[kAuV2NumPeriods])
{
    dmaSetMode(0, 1, kAuV2AspFmtS16LE, 1);
    dmaSetBuffers(pagePhys);
    srcSet48kPlayback();
}

void AuVortex2Driver::startPlaybackDMA(void)
{
    fFifoEnabled = 1;
    dmaStartFifo();
}

void AuVortex2Driver::stopPlaybackDMA(void)
{
    fFifoEnabled = 0;
    dmaStopFifo();
}

void AuVortex2Driver::programCaptureDMA(IOPhysicalAddress pagePhys[kAuV2NumPeriods])
{
    UInt32 psize = kAuV2PeriodBytes;
    UInt32 cfg0 = 0;
    UInt32 cfg1 = 0;
    int ch = kAuV2CapDMAChannel;

    fCapDmaCtrl = (fCapDmaCtrl & ~OFFSET_MASK);
    fCapDmaCtrl = (fCapDmaCtrl & ~IE_MASK);
    fCapDmaCtrl = (fCapDmaCtrl & ~DIR_MASK);
    fCapDmaCtrl = (fCapDmaCtrl & ~FMT_MASK) |
                  ((kAuV2AspFmtS16LE << FMT_SHIFT) & FMT_MASK);
    writeRegister(VORTEX_ADBDMA_CTRL + (ch << 2), fCapDmaCtrl);
    (void)readRegister(VORTEX_ADBDMA_CTRL + (ch << 2));

    cfg1 |= 0x88000000 | 0x44000000 | 0x30000000 | (psize - 1);
    writeRegister(VORTEX_ADBDMA_BUFBASE + (ch << 4) + 0xc, (UInt32)pagePhys[3]);
    cfg0 |= 0x12000000;
    cfg1 |= 0x80000000 | 0x40000000 | ((psize - 1) << 0xc);
    writeRegister(VORTEX_ADBDMA_BUFBASE + (ch << 4) + 0x8, (UInt32)pagePhys[2]);
    cfg0 |= 0x88000000 | 0x44000000 | 0x10000000 | (psize - 1);
    writeRegister(VORTEX_ADBDMA_BUFBASE + (ch << 4) + 0x4, (UInt32)pagePhys[1]);
    cfg0 |= 0x80000000 | 0x40000000 | ((psize - 1) << 0xc);
    writeRegister(VORTEX_ADBDMA_BUFBASE + (ch << 4), (UInt32)pagePhys[0]);

    writeRegister(VORTEX_ADBDMA_BUFCFG0 + (ch << 3), cfg0);
    writeRegister(VORTEX_ADBDMA_BUFCFG1 + (ch << 3), cfg1);
    writeRegister(VORTEX_ADBDMA_CTRL + (ch << 2), fCapDmaCtrl);
    writeRegister(VORTEX_ADBDMA_START + (ch << 2),
                  0 << ((0xf - (ch & 0xf)) * 2));
    srcSet48kCapture();
}

void AuVortex2Driver::startCaptureDMA(void)
{
    int empty = 0;
    int priority = 0;
    int ch = kAuV2CapDMAChannel;

    fCapFifoEnabled = 1;
    switch (fCapFifoStatus) {
    case kAuV2FifoStart:
        fifoSetAdbValid(ch, 1);
        break;
    case kAuV2FifoStop:
        empty = 1;
        writeRegister(VORTEX_ADBDMA_CTRL + (ch << 2), fCapDmaCtrl);
        fifoSetAdbCtrl(ch, 1, priority, empty, 1, 0);
        break;
    case kAuV2FifoPause:
        fifoSetAdbCtrl(ch, 1, priority, empty, 1, 0);
        break;
    }
    fCapFifoStatus = kAuV2FifoStart;
}

void AuVortex2Driver::stopCaptureDMA(void)
{
    int ch = kAuV2CapDMAChannel;
    if (fCapFifoStatus == kAuV2FifoStart)
        fifoSetAdbCtrl(ch, 1, 0, 0, 0, 0);
    else if (fCapFifoStatus == kAuV2FifoStop)
        return;
    fCapFifoStatus = kAuV2FifoStop;
    fCapFifoEnabled = 0;
}

UInt32 AuVortex2Driver::getDmaBytePosition(void)
{
    UInt32 temp = readRegister(VORTEX_ADBDMA_STAT + (kAuV2DMAChannel << 2));
    UInt32 page = (temp & ADB_SUBBUF_MASK) >> ADB_SUBBUF_SHIFT;
    UInt32 pos = (page * kAuV2PeriodBytes) + (temp & (kAuV2PeriodBytes - 1));
    if (pos >= kAuV2BufferBytes)
        pos %= kAuV2BufferBytes;
    return pos;
}

bool AuVortex2Driver::initializeVortexHardware(void)
{
    /* Never write 0xffffffff to VORTEX_CTRL: CTRL_IRQ_ENABLE storms the shared PCI IRQ. */
    UInt32 ctrl = readRegister(VORTEX_CTRL);
    writeRegister(VORTEX_CTRL, (ctrl | CTRL_RST) & ~CTRL_IRQ_ENABLE);
    IOSleep(5);
    writeRegister(VORTEX_CTRL, (readRegister(VORTEX_CTRL) & ~(CTRL_RST | CTRL_IRQ_ENABLE | 0x00200000)));
    IOSleep(5);
    writeRegister(VORTEX_IRQ_CTRL, 0);
    writeRegister(VORTEX_IRQ_SOURCE, 0xffffffff);
    (void)readRegister(VORTEX_IRQ_STAT);

    codecInit();
    writeRegister(VORTEX_CTRL, (readRegister(VORTEX_CTRL) | 0x1000000) & ~CTRL_IRQ_ENABLE);
    writeRegister(VORTEX_ENGINE_CTRL, 0);
    adbInit();
    fifoInit();
    mixerInit();
    srcInit();
    connectAnalogPlayback(1);
    connectAnalogCapture(1);
    connectSpdifPlayback(1);
    srcSet48kPlayback();
    srcSet48kCapture();
    spdifInit(48000);
    if (!initAC97())
        return false;

    writeRegister(VORTEX_IRQ_CTRL, 0);
    writeRegister(VORTEX_CTRL, readRegister(VORTEX_CTRL) & ~CTRL_IRQ_ENABLE);
    fFifoStatus = kAuV2FifoStop;
    fFifoEnabled = 0;
    LOG("hardware init done (PCI IRQ disabled)");
    return true;
}

void AuVortex2Driver::shutdownHardware(void)
{
    if (!fBaseAddress)
        return;
    stopPlaybackDMA();
    stopCaptureDMA();
    writeRegister(VORTEX_CTRL, readRegister(VORTEX_CTRL) & ~CTRL_IRQ_ENABLE);
    fifoInit();
    adbInit();
    writeRegister(VORTEX_IRQ_CTRL, 0);
    writeRegister(VORTEX_CTRL, 0);
    IOSleep(5);
    writeRegister(VORTEX_IRQ_SOURCE, 0xffff);
}

bool AuVortex2Driver::setupTimer(void)
{
    IOWorkLoop *wl = getWorkLoop();
    if (!wl)
        return false;
    fTimerSource = IOTimerEventSource::timerEventSource(this, timerFired);
    if (!fTimerSource)
        return false;
    if (wl->addEventSource(fTimerSource) != kIOReturnSuccess) {
        fTimerSource->release();
        fTimerSource = NULL;
        return false;
    }
    return true;
}

void AuVortex2Driver::startStampTimer(void)
{
    if (fTimerSource)
        fTimerSource->setTimeoutMS(5);
}

void AuVortex2Driver::stopStampTimer(void)
{
    if (fTimerSource)
        fTimerSource->cancelTimeout();
}

void AuVortex2Driver::timerFired(OSObject *owner, IOTimerEventSource *sender)
{
    AuVortex2Driver *driver = OSDynamicCast(AuVortex2Driver, owner);
    (void)sender;
    if (driver && driver->fAudioEngine)
        driver->fAudioEngine->pollTimeStamp();
    if (driver && driver->fTimerSource)
        driver->fTimerSource->setTimeoutMS(5);
}

void AuVortex2Driver::handleInterrupt(void)
{
}

bool AuVortex2AudioEngine::init(AuVortex2Driver *device)
{
    if (!device)
        return false;
    if (!IOAudioEngine::init(NULL))
        return false;
    fDevice = device;
    fOutputBuffer = NULL;
    fOutputBufferPtr = NULL;
    fOutputStream = NULL;
    fInputBuffer = NULL;
    fInputBufferPtr = NULL;
    fInputStream = NULL;
    fLastSampleFrame = 0;
    bzero(fOutputPhys, sizeof(fOutputPhys));
    bzero(fInputPhys, sizeof(fInputPhys));
    return true;
}

void AuVortex2AudioEngine::free(void)
{
    if (fOutputBuffer) {
        fOutputBuffer->release();
        fOutputBuffer = NULL;
        fOutputBufferPtr = NULL;
    }
    if (fInputBuffer) {
        fInputBuffer->release();
        fInputBuffer = NULL;
        fInputBufferPtr = NULL;
    }
    fOutputStream = NULL;
    fInputStream = NULL;
    IOAudioEngine::free();
}

bool AuVortex2AudioEngine::initHardware(IOService *provider)
{
    IOAudioSampleRate sampleRate;
    if (!IOAudioEngine::initHardware(provider))
        return false;
    setDescription("Aureal Vortex 2 Output");
    setNumSampleFramesPerBuffer(kAuV2BufferFrames);
    setSampleOffset(kAuV2PeriodFrames);
    setSampleLatency(kAuV2PeriodFrames);
    sampleRate.whole = kAuV2SampleRate;
    sampleRate.fraction = 0;
    setSampleRate(&sampleRate);
    fOutputBuffer = IOBufferMemoryDescriptor::withOptions(
        kIOMemoryPhysicallyContiguous,
        kAuV2BufferBytes, PAGE_SIZE);
    if (!fOutputBuffer) {
        LOG("output buffer alloc failed");
        return false;
    }
    if (fOutputBuffer->prepare(kIODirectionInOut) != kIOReturnSuccess) {
        LOG("output buffer prepare failed");
        return false;
    }
    fOutputBufferPtr = fOutputBuffer->getBytesNoCopy();
    if (!fOutputBufferPtr) {
        LOG("output buffer ptr failed");
        return false;
    }
    bzero(fOutputBufferPtr, kAuV2BufferBytes);
    {
        IOByteCount len = 0;
        IOPhysicalAddress base;
        UInt32 i;
        base = fOutputBuffer->getPhysicalSegment(0, &len);
        if (base == 0) {
            LOG("DMA physical address failed");
            return false;
        }
        if (len < kAuV2BufferBytes)
            LOG("warning: DMA buffer not fully contiguous (%lu of %u)", (unsigned long)len, kAuV2BufferBytes);
        for (i = 0; i < kAuV2NumPeriods; i++) {
            IOByteCount seglen = 0;
            fOutputPhys[i] = fOutputBuffer->getPhysicalSegment(i * kAuV2PeriodBytes, &seglen);
            if (fOutputPhys[i] == 0)
                fOutputPhys[i] = base + (i * kAuV2PeriodBytes);
            LOG("DMA page %u phys=0x%08x seg=%lu", i, (unsigned)fOutputPhys[i], (unsigned long)seglen);
        }
    }
    fInputBuffer = IOBufferMemoryDescriptor::withOptions(
        kIOMemoryPhysicallyContiguous,
        kAuV2BufferBytes, PAGE_SIZE);
    if (!fInputBuffer) {
        LOG("input buffer alloc failed");
        return false;
    }
    if (fInputBuffer->prepare(kIODirectionInOut) != kIOReturnSuccess) {
        LOG("input buffer prepare failed");
        return false;
    }
    fInputBufferPtr = fInputBuffer->getBytesNoCopy();
    if (!fInputBufferPtr) {
        LOG("input buffer ptr failed");
        return false;
    }
    bzero(fInputBufferPtr, kAuV2BufferBytes);
    {
        IOByteCount len = 0;
        IOPhysicalAddress base;
        UInt32 i;
        base = fInputBuffer->getPhysicalSegment(0, &len);
        if (base == 0) {
            LOG("input DMA physical address failed");
            return false;
        }
        for (i = 0; i < kAuV2NumPeriods; i++) {
            IOByteCount seglen = 0;
            fInputPhys[i] = fInputBuffer->getPhysicalSegment(i * kAuV2PeriodBytes, &seglen);
            if (fInputPhys[i] == 0)
                fInputPhys[i] = base + (i * kAuV2PeriodBytes);
            LOG("input DMA page %u phys=0x%08x seg=%lu", i, (unsigned)fInputPhys[i], (unsigned long)seglen);
        }
    }
    if (!createOutputStream()) {
        LOG("createOutputStream failed");
        return false;
    }
    if (!createInputStream()) {
        LOG("createInputStream failed");
        return false;
    }

    if (!createAudioControls())

        LOG("warning: volume controls not created");
    LOG("engine hardware ready");
    return true;
}

void AuVortex2AudioEngine::stop(IOService *provider)
{
    IOAudioEngine::stop(provider);
}

bool AuVortex2AudioEngine::createOutputStream(void)
{
    IOAudioStreamFormat format;
    IOAudioSampleRate sampleRate;
    fOutputStream = new IOAudioStream;
    if (!fOutputStream) {
        LOG("stream alloc failed");
        return false;
    }
    if (!fOutputStream->initWithAudioEngine(this, kIOAudioStreamDirectionOutput, kAuV2Channels)) {
        LOG("stream init failed");
        return false;
    }
    bzero(&format, sizeof(format));
    format.fNumChannels = kAuV2Channels;
    format.fSampleFormat = kIOAudioStreamSampleFormatLinearPCM;
    format.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
    format.fBitDepth = 16;
    format.fBitWidth = 16;
    format.fAlignment = kIOAudioStreamAlignmentLowByte;
    format.fByteOrder = kIOAudioStreamByteOrderLittleEndian;
    format.fIsMixable = true;
    format.fDriverTag = 0;
    sampleRate.whole = kAuV2SampleRate;
    sampleRate.fraction = 0;
    fOutputStream->setSampleBuffer(fOutputBufferPtr, kAuV2BufferBytes);
    fOutputStream->addAvailableFormat(&format, &sampleRate, &sampleRate);
    fOutputStream->setFormat(&format);
    addAudioStream(fOutputStream);
    fOutputStream->release();
    return true;
}

bool AuVortex2AudioEngine::createInputStream(void)
{
    IOAudioStreamFormat format;
    IOAudioSampleRate sampleRate;
    fInputStream = new IOAudioStream;
    if (!fInputStream) {
        LOG("input stream alloc failed");
        return false;
    }
    if (!fInputStream->initWithAudioEngine(this, kIOAudioStreamDirectionInput, kAuV2Channels)) {
        LOG("input stream init failed");
        return false;
    }
    bzero(&format, sizeof(format));
    format.fNumChannels = kAuV2Channels;
    format.fSampleFormat = kIOAudioStreamSampleFormatLinearPCM;
    format.fNumericRepresentation = kIOAudioStreamNumericRepresentationSignedInt;
    format.fBitDepth = 16;
    format.fBitWidth = 16;
    format.fAlignment = kIOAudioStreamAlignmentLowByte;
    format.fByteOrder = kIOAudioStreamByteOrderLittleEndian;
    format.fIsMixable = false;
    format.fDriverTag = 0;
    sampleRate.whole = kAuV2SampleRate;
    sampleRate.fraction = 0;
    fInputStream->setSampleBuffer(fInputBufferPtr, kAuV2BufferBytes);
    fInputStream->addAvailableFormat(&format, &sampleRate, &sampleRate);
    fInputStream->setFormat(&format);
    addAudioStream(fInputStream);
    fInputStream->release();
    return true;
}

bool AuVortex2AudioEngine::createAudioControls(void)
{
    IOAudioLevelControl *volume;
    IOAudioToggleControl *mute;

    volume = IOAudioLevelControl::createVolumeControl(
        fDevice->fMasterVolume,
        kAuV2VolumeMin,
        kAuV2VolumeMax,
        (-46 << 16) + 32768,
        0,
        kIOAudioControlChannelIDAll,
        kIOAudioControlChannelNameAll,
        0,
        kIOAudioControlUsageOutput);
    if (!volume) {
        LOG("volume control alloc failed");
        return false;
    }
    volume->setValueChangeHandler(AuVortex2Driver::volumeChangeHandler, fDevice);
    addDefaultAudioControl(volume);
    volume->release();

    mute = IOAudioToggleControl::createMuteControl(
        false,
        kIOAudioControlChannelIDAll,
        kIOAudioControlChannelNameAll,
        0,
        kIOAudioControlUsageOutput);
    if (!mute) {
        LOG("mute control alloc failed");
        return false;
    }
    mute->setValueChangeHandler(AuVortex2Driver::muteChangeHandler, fDevice);
    addDefaultAudioControl(mute);
    mute->release();

    volume = IOAudioLevelControl::createVolumeControl(
        fDevice->fInputVolume,
        kAuV2VolumeMin,
        kAuV2VolumeMax,
        (-22 << 16) + 32768,
        (22 << 16) + 32768,
        kIOAudioControlChannelIDAll,
        kIOAudioControlChannelNameAll,
        1,
        kIOAudioControlUsageInput);
    if (!volume) {
        LOG("input volume control alloc failed");
        return false;
    }
    volume->setValueChangeHandler(AuVortex2Driver::inputVolumeChangeHandler, fDevice);
    addDefaultAudioControl(volume);
    volume->release();

    mute = IOAudioToggleControl::createMuteControl(
        false,
        kIOAudioControlChannelIDAll,
        kIOAudioControlChannelNameAll,
        1,
        kIOAudioControlUsageInput);
    if (!mute) {
        LOG("input mute control alloc failed");
        return false;
    }
    mute->setValueChangeHandler(AuVortex2Driver::inputMuteChangeHandler, fDevice);
    addDefaultAudioControl(mute);
    mute->release();
    return true;
}

IOReturn AuVortex2AudioEngine::performAudioEngineStart(void)
{
    fLastSampleFrame = 0;
    takeTimeStamp(false);
    fDevice->programPlaybackDMA(fOutputPhys);
    fDevice->programCaptureDMA(fInputPhys);
    fDevice->startPlaybackDMA();
    fDevice->startCaptureDMA();
    fDevice->startStampTimer();
    return kIOReturnSuccess;
}

IOReturn AuVortex2AudioEngine::performAudioEngineStop(void)
{
    fDevice->stopStampTimer();
    fDevice->stopPlaybackDMA();
    fDevice->stopCaptureDMA();
    return kIOReturnSuccess;
}

UInt32 AuVortex2AudioEngine::getCurrentSampleFrame(void)
{
    return fDevice->getDmaBytePosition() / kAuV2FrameBytes;
}

IOReturn AuVortex2AudioEngine::performFormatChange(IOAudioStream *audioStream,
    const IOAudioStreamFormat *newFormat, const IOAudioSampleRate *newSampleRate)
{
    return kIOReturnSuccess;
}

IOReturn AuVortex2AudioEngine::clipOutputSamples(const void *mixBuf, void *sampleBuf,
    UInt32 firstSampleFrame, UInt32 numSampleFrames,
    const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream)
{
    UInt32 numSamples = numSampleFrames * streamFormat->fNumChannels;
    float *in = ((float *)mixBuf) + (firstSampleFrame * streamFormat->fNumChannels);
    SInt16 *out = ((SInt16 *)sampleBuf) + (firstSampleFrame * streamFormat->fNumChannels);
    UInt32 i;
    for (i = 0; i < numSamples; i++) {
        float s = in[i];
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        out[i] = (SInt16)(s * 32767.0f);
    }
    return kIOReturnSuccess;
}

IOReturn AuVortex2AudioEngine::convertInputSamples(const void *sampleBuf, void *destBuf,
    UInt32 firstSampleFrame, UInt32 numSampleFrames,
    const IOAudioStreamFormat *streamFormat, IOAudioStream *audioStream)
{
    UInt32 numSamples = numSampleFrames * streamFormat->fNumChannels;
    const SInt16 *in = ((const SInt16 *)sampleBuf) + (firstSampleFrame * streamFormat->fNumChannels);
    float *out = ((float *)destBuf) + (firstSampleFrame * streamFormat->fNumChannels);
    UInt32 i;
    (void)audioStream;
    for (i = 0; i < numSamples; i++)
        out[i] = ((float)in[i]) / 32768.0f;
    return kIOReturnSuccess;
}

void AuVortex2AudioEngine::pollTimeStamp(void)
{
    UInt32 frame = getCurrentSampleFrame();

    /* takeTimeStamp() means one full sample buffer wrapped, not one timer tick.
       A 10 ms tick on a 2048-frame / 48 kHz buffer (~42.7 ms) made CoreAudio
       run about 4x fast (video racing, clipIfNecessary). */
    if (frame < fLastSampleFrame)
        takeTimeStamp(true);
    fLastSampleFrame = frame;
}
