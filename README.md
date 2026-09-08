# Aureal-Vortex2-Au8830-OSX
The world's first native 64-bit IOKit driver and installer for Aureal Vortex 2 (AU8830) on Mac OS X 10.6.8
# Aureal Vortex 2 (AU8830) Native 64-bit Driver for macOS 10.6.8 (Snow Leopard)

This is the world's first fully functional, native 64-bit IOKit driver (kext) for the legendary **Aureal Vortex 2 (AU8830)** sound card, built from scratch for Mac OS X Snow Leopard.

For nearly three decades, this revolutionary DSP chip was considered "dead" for modern Apple systems due to the company's liquidation and closed-source documentation. This project successfully reverse-engineers the hardware paths based on Linux ALSA and maps them cleanly into Apple's `IOAudioFamily` object model.

## ✨ Features
* **Crystal Clear 16-bit 48kHz Stereo Output** via custom low-overhead PCM clipping.
* **Hardware-Accelerated 3D Surround Sound** running simultaneously on BOTH green analog outputs of the SigmaTel STAC9708 codec.
* **100% Functional Hardware Mute and Vol Control** working smoothly via safe register gating.
* **Full Audio Input & Capture Support** for Microphone/Line-In using a dedicated secondary capturing DMA channel.
* **Simultaneous S/PDIF Digital Output** working natively alongside analog playback.

---

# Драйвер Aureal Vortex 2 (AU8830) x64 для Mac OS X 10.6.8 (Snow Leopard)

Это первый в мире полностью функциональный нативный 64-битный драйвер (kext) архитектуры IOKit для легендарной звуковой карты **Aureal Vortex 2 (AU8830)**, созданный с нуля для Mac OS X Snow Leopard.

Почти 30 лет этот революционный чип DSP считался «мертвым» для современных систем Apple из-за ликвидации компании Aureal и закрытой документации. Данный проект переносит низкоуровневую логику коммутации из Linux ALSA и чисто интегрирует её в объектную модель `IOAudioFamily` ядра Apple.

## ✨ Ключевые возможности
* **Кристально чистый стереовыход 16-бит/48кГц** через кастомную функцию клиппинга.
* **Аппаратный объемный звук 3D Surround**, работающий СИНХРОННО на обоих зеленых аналоговых гнездах кодека SigmaTel STAC9708.
* **100% тишина при Mute и плавная регулировка** громкости за счет точной синхронизации регистров.
* **Полноценная запись звука (Микрофон / Линейный вход)**, реализованная через выделенный второй DMA-канал захвата.
* **Синхронный цифровой выход S/PDIF**, работающий параллельно с аналоговыми гнездами.

## 🛠️ Technical Details / Технические детали
* **Playback Route:** ADB DMA0 ➔ SRC0/1 ➔ Mixin 0/1 ➔ Mixout 0/1 + 2/3 ➔ AC97 DAC (with 0x20/0x22 SigmaTel 3D Matrix passthrough).
* **Capture Route:** AC97 ADC ➔ Mixin 2/3 ➔ Mixout 4/5 ➔ SRC2/3 ➔ ADB DMA1.
* **Timer Sync:** Hardware ring-buffer tracking via non-blocking periodic thread mapping (pollTimeStamp).

## 🏆 Credits / Авторство
Developed by **iDenis** (2026). 
Special thanks to the ALSA au88x0 open-source developers for the original reverse-engineering foundations.
