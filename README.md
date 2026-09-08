# Aureal-Vortex2-Au8830-OSX
This is the world's first fully functional, native 64-bit IOKit driver (kext) and installer for the legendary **Aureal Vortex 2 (AU8830)** sound card, supporting the entire Intel-macOS era from Snow Leopard to Tahoe.
# Aureal Vortex 2 (AU8830) Universal 64-bit Driver for Intel macOS (10.6.8 – 26.5 Tahoe)

This is the world's first fully functional, native 64-bit IOKit driver (kext) for the legendary **Aureal Vortex 2 (AU8830)** sound card, built from scratch for Mac OS X Snow Leopard.

For nearly three decades, this revolutionary DSP chip was considered "dead" for modern Apple systems due to the company's liquidation and closed-source documentation. This project successfully reverse-engineers the hardware paths based on Linux ALSA and maps them cleanly into Apple's `IOAudioFamily` object model.

## ✨ Features
* **Crystal Clear 16-bit 48kHz Stereo Output** via custom low-overhead PCM clipping.
* **Hardware-Accelerated 3D Surround Sound** running simultaneously on BOTH green analog outputs of the SigmaTel STAC9708 codec.
* **100% Functional Hardware Mute and Vol Control** working smoothly via safe register gating.
* **Full Audio Input & Capture Support** for Microphone/Line-In using a dedicated secondary capturing DMA channel.
* **Simultaneous S/PDIF Digital Output** working natively alongside analog playback.
## 💻 OS Compatibility / Совместимость с ОС
Tested and fully verified across the entire Intel-macOS era / Проверено и полностью подтверждено для всей Intel-эры macOS:
* **Mac OS X 10.6.8 (Snow Leopard)** [Intel x64] — Native baseline
* **OS X 10.7 – macOS 10.15** (Lion, Mountain Lion, Mavericks, Yosemite, El Capitan, Sierra, High Sierra, Mojave, Catalina) [Intel x64]
* **macOS 11.0 – macOS 26.5 (Tahoe)** (Big Sur, Monterey, Ventura, Sonoma, Sequoia, Tahoe) [Intel x64]

*Note: Requires an Intel processor. Designed to work flawlessly with modern Intel Hackintosh setups using legacy PCI-e to PCI adapters.*
*Примечание: Требуется процессор Intel. Разработано для безупречной работы на современных Intel-хакинтошах через переходники PCI-e -> PCI.*

---

# Универсальный драйвер Aureal Vortex 2 (AU8830) x64 для Intel macOS (от 10.6.8 до 26.5 Tahoe)

Это первый в мире полностью функциональный нативный 64-битный драйвер (kext) и инсталлятор архитектуры IOKit для легендарной звуковой карты **Aureal Vortex 2 (AU8830)**, поддерживающий всю Intel-эру систем macOS — от Snow Leopard до новейшей Tahoe.

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

## 🛠 Installation for Modern macOS (10.14 – 26.5 Tahoe) / Установка на современные системы

For macOS Mojave (10.14) and newer, do NOT use the `.pkg` installer. Instead, inject the kext via your bootloader (OpenCore is highly recommended) / Для macOS Mojave (10.14) и новее НЕ используйте `.pkg` инсталлятор. Внедряйте кекст через ваш загрузчик (настоятельно рекомендуется OpenCore):

### 🍏 OpenCore Method:
1. Download **`AuVortex2Driver-kext-only.zip`** from the Releases page and extract it.
2. Copy `AuVortex2Driver.kext` to your EFI folder: `/EFI/OC/Kexts/`.
3. Open your `config.plist` using proper editor (like OpenCore Configurator or ProperTree).
4. Add the kext to the **Kernel -> Add** section.
5. Save `config.plist` and reboot your Mac.

### 🍏 Метод для OpenCore:
1. Скачайте **`AuVortex2Driver-kext-only.zip`** со страницы релизов и распакуйте его.
2. Скопируйте `AuVortex2Driver.kext` в папку вашего EFI: `/EFI/OC/Kexts/`.
3. Откройте ваш `config.plist` через редактор (например, OpenCore Configurator или ProperTree).
4. Добавьте кекст в секцию **Kernel -> Add**.
5. Сохраните `config.plist` и перезагрузите компьютер.

