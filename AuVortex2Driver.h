/*
 * AuVortex2Driver.h
 * Aureal Vortex 2 (AU8830) — IOAudio for Mac OS X 10.6.8
 *
 * Playback:  ADB DMA0 -> SRC0/1 -> mixin 0/1 -> mixout 0/1 + 2/3 -> AC97 DAC
 * Capture:   AC97 ADC -> mixin 2/3 -> mixout 4/5 -> SRC2/3 -> ADB DMA1
 * SPDIF:     same PCM mixins -> mixout 6/7 -> ADB_SPDIFOUT (48 kHz IEC958)
 */

#ifndef _AuVortex2Driver_h
#define _AuVortex2Driver_h

#include <IOKit/audio/IOAudioDevice.h>
#include <IOKit/audio/IOAudioEngine.h>
#include <IOKit/audio/IOAudioStream.h>
#include <IOKit/audio/IOAudioControl.h>
#include <IOKit/audio/IOAudioLevelControl.h>
#include <IOKit/audio/IOAudioToggleControl.h>
#include <IOKit/audio/IOAudioDefines.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOBufferMemoryDescriptor.h>

class AuVortex2Driver;
class AuVortex2AudioEngine;

/* 48 kHz, S16LE stereo, 4 hardware DMA pages of 512 frames */
#define kAuV2SampleRate        48000
#define kAuV2Channels          2
#define kAuV2BytesPerSample    2
#define kAuV2PeriodFrames      512
#define kAuV2NumPeriods        4
#define kAuV2BufferFrames      (kAuV2PeriodFrames * kAuV2NumPeriods)
#define kAuV2FrameBytes        (kAuV2Channels * kAuV2BytesPerSample)
#define kAuV2PeriodBytes       (kAuV2PeriodFrames * kAuV2FrameBytes)
#define kAuV2BufferBytes       (kAuV2BufferFrames * kAuV2FrameBytes)

#define kAuV2DMAChannel        0
#define kAuV2SrcLeft           0
#define kAuV2SrcRight          1
#define kAuV2MixinLeft         0
#define kAuV2MixinRight        1
#define kAuV2MixLeft           0
#define kAuV2MixRight          1
#define kAuV2MixRearLeft       2
#define kAuV2MixRearRight      3
#define kAuV2ADBChannel        0x11
#define kAuV2DmaRouteCh        1   /* Linux uses src[nr_ch-1] as ADB channel */

#define kAuV2CapDMAChannel     1
#define kAuV2CapSrcLeft        2
#define kAuV2CapSrcRight       3
#define kAuV2CapMixinLeft      2
#define kAuV2CapMixinRight     3
#define kAuV2CapMixLeft        4
#define kAuV2CapMixRight       5
#define kAuV2CapRouteCh        3   /* Linux uses src[nr_ch-1] for capture LRT */

#define kAuV2SpdifMixLeft      6
#define kAuV2SpdifMixRight     7
#define kAuV2SpdifChannel      0x14

#define kAuV2FifoStop          0
#define kAuV2FifoStart         1
#define kAuV2FifoPause         2

#define kAuV2AspFmtS16LE       0x8
#define kAuV2IrqPcmOut         0x0020
#define kAuV2IrqErrMask        0x00ff
#define kAuV2IrqTimer          0x1000

#define kAuV2VolumeMin         0
#define kAuV2VolumeMax         65535
#define kAuV2VolumeDefault     32768

class AuVortex2Driver : public IOAudioDevice
{
    OSDeclareDefaultStructors(AuVortex2Driver)
    friend class AuVortex2AudioEngine;

private:
    IOPCIDevice                    *fPCIDevice;
    IOMemoryMap                    *fMMIO;
    volatile UInt8                 *fBaseAddress;
    IOTimerEventSource              *fTimerSource;
    AuVortex2AudioEngine           *fAudioEngine;
    bool                            fDeviceInitialized;

    UInt16                          fVendorID;
    UInt16                          fDeviceID;

    int                             fMixChannels[16];
    UInt32                          fDmaCtrl;
    int                             fFifoStatus;
    int                             fFifoEnabled;
    UInt32                          fCapDmaCtrl;
    int                             fCapFifoStatus;
    int                             fCapFifoEnabled;
    int                             fPeriodReal;
    int                             fPeriodVirt;
    UInt32                          fPendingIrq;
    SInt32                          fMasterVolume;
    bool                            fMuted;
    SInt32                          fInputVolume;
    bool                            fInputMuted;

public:
    virtual bool init(OSDictionary *properties);
    virtual void free(void);
    virtual IOService *probe(IOService *provider, SInt32 *score);
    virtual void stop(IOService *provider);
    virtual bool initHardware(IOService *provider);

    UInt32 readRegister(UInt32 offset);
    void   writeRegister(UInt32 offset, UInt32 value);

    void   handleInterrupt(void);
    UInt32 getDmaBytePosition(void);

    void   programPlaybackDMA(IOPhysicalAddress pagePhys[kAuV2NumPeriods]);
    void   startPlaybackDMA(void);
    void   stopPlaybackDMA(void);
    void   programCaptureDMA(IOPhysicalAddress pagePhys[kAuV2NumPeriods]);
    void   startCaptureDMA(void);
    void   stopCaptureDMA(void);
    void   applyAnalogVolume(void);
    void   applyAnalogInputGain(void);

    static IOReturn volumeChangeHandler(OSObject *target, IOAudioControl *control,
                                        SInt32 oldValue, SInt32 newValue);
    static IOReturn muteChangeHandler(OSObject *target, IOAudioControl *control,
                                      SInt32 oldValue, SInt32 newValue);
    static IOReturn inputVolumeChangeHandler(OSObject *target, IOAudioControl *control,
                                             SInt32 oldValue, SInt32 newValue);
    static IOReturn inputMuteChangeHandler(OSObject *target, IOAudioControl *control,
                                           SInt32 oldValue, SInt32 newValue);

private:
    bool configurePCIDevice(void);
    bool mapHardwareRegisters(void);
    bool initializeVortexHardware(void);
    void shutdownHardware(void);
    bool setupTimer(void);
    void startStampTimer(void);
    void stopStampTimer(void);

    void codecWrite(UInt8 addr, UInt16 data);
    UInt16 codecRead(UInt8 addr);
    void codecInit(void);
    bool initAC97(void);

    void mixerInit(void);
    void mixSetOutputVol(UInt8 mix, UInt8 vol);
    void mixSetInputVol(UInt8 mix, UInt8 mixin, UInt8 vol);
    void mixSetEnableBit(UInt8 mix, int mixin, int en);
    void mixEnableInput(UInt8 mix, int mixin);
    void mixDisableInput(UInt8 mix, int mixin);
    void mixerEnSr(int channel);
    void mixerAddWTD(UInt8 mix, UInt8 ch);

    void fifoClearAdbData(int fifo);
    void fifoInit(void);
    void fifoSetAdbValid(int fifo, int en);
    void fifoSetAdbCtrl(int fifo, int stereo, int priority,
                        int empty, int valid, int f);

    void adbInit(void);
    void adbEnSr(int channel);
    void adbAddRoute(UInt8 channel, UInt32 route);
    void adbRoute(int en, UInt8 channel, UInt8 source, UInt8 dest);

    void srcInit(void);
    void srcEnSr(int channel);
    void srcAddWTD(UInt8 src, UInt8 ch);
    void srcFlush(UInt8 src);
    void srcClearDrift(UInt8 src);
    void srcSetThrottle(UInt8 src, int en);
    void srcPersistRatio(UInt8 src, UInt32 ratio);
    void srcSetup(UInt8 src, UInt32 cr, int d, int dirplay);
    void srcSet48kPlayback(void);
    void srcSet48kCapture(void);

    void connectionMixinMix(int en, UInt8 mixin, UInt8 mix);
    void connectionMixAdb(int en, UInt8 ch, UInt8 mix, UInt8 dest);
    void connectAnalogPlayback(int en);
    void connectAnalogCapture(int en);
    void connectSpdifPlayback(int en);
    void spdifInit(int rate);
    void adbRouteLRT(int en, UInt8 ch, UInt8 source0, UInt8 source1, UInt8 dest);

    void dmaSetMode(int ie, int dir, int fmt, int stereo);
    void dmaSetBuffers(IOPhysicalAddress pagePhys[kAuV2NumPeriods]);
    void dmaStartFifo(void);
    void dmaStopFifo(void);
    void dmaSetModeCh(int ch, UInt32 *ctrl, int ie, int dir, int fmt, int stereo);
    void dmaSetBuffersCh(int ch, UInt32 *ctrl, IOPhysicalAddress pagePhys[kAuV2NumPeriods]);
    void dmaStartFifoCh(int ch, int *status, int enabled, UInt32 ctrl);
    void dmaStopFifoCh(int ch, int *status, int *enabled);

    static void timerFired(OSObject *owner, IOTimerEventSource *sender);
};

class AuVortex2AudioEngine : public IOAudioEngine
{
    OSDeclareDefaultStructors(AuVortex2AudioEngine)

private:
    AuVortex2Driver            *fDevice;
    IOBufferMemoryDescriptor   *fOutputBuffer;
    void                       *fOutputBufferPtr;
    IOPhysicalAddress           fOutputPhys[kAuV2NumPeriods];
    IOAudioStream              *fOutputStream;
    IOBufferMemoryDescriptor   *fInputBuffer;
    void                       *fInputBufferPtr;
    IOPhysicalAddress           fInputPhys[kAuV2NumPeriods];
    IOAudioStream              *fInputStream;
    UInt32                      fLastSampleFrame;

public:
    virtual bool init(AuVortex2Driver *device);
    virtual void free(void);

    virtual bool initHardware(IOService *provider);
    virtual void stop(IOService *provider);

    virtual IOReturn performAudioEngineStart(void);
    virtual IOReturn performAudioEngineStop(void);
    virtual UInt32 getCurrentSampleFrame(void);
    virtual IOReturn performFormatChange(IOAudioStream *audioStream,
                                         const IOAudioStreamFormat *newFormat,
                                         const IOAudioSampleRate *newSampleRate);
    virtual IOReturn clipOutputSamples(const void *mixBuf,
                                       void *sampleBuf,
                                       UInt32 firstSampleFrame,
                                       UInt32 numSampleFrames,
                                       const IOAudioStreamFormat *streamFormat,
                                       IOAudioStream *audioStream);
    virtual IOReturn convertInputSamples(const void *sampleBuf,
                                         void *destBuf,
                                         UInt32 firstSampleFrame,
                                         UInt32 numSampleFrames,
                                         const IOAudioStreamFormat *streamFormat,
                                         IOAudioStream *audioStream);

    void pollTimeStamp(void);

private:
    bool createOutputStream(void);
    bool createInputStream(void);
    bool createAudioControls(void);
};

#endif /* _AuVortex2Driver_h */
