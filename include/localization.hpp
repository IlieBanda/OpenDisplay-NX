#pragma once
#include <cstring>
namespace locale {
enum class Language { English, Russian, Spanish, Chinese };
enum Key {
  Connected,
  Connect,
  Instruction1,
  Instruction2,
  Wifi,
  Address,
  Decoder,
  Statistics,
  Automatic,
  On,
  Off,
  LanguageLabel,
  Select,
  Confirm,
  StatsKey,
  DisconnectKey,
  DecoderKey,
  ExitKey,
  Back,
  SettingsHint,
  Idle,
  Streaming,
  DesktopIdle,
  Delay,
  Waiting,
  SoftwareWaiting,
  HardwareWaiting,
  NetworkChanged,
  DecoderFailed,
  WaitingVideo,
  VideoConnected,
  MenuKey,
  SettingsTitle,
  Count
};
inline constexpr const char *strings[Count][4] = {
    {"Display connected", "Экран подключён", "Pantalla conectada",
     "显示器已连接"},
    {"Connect to your Mac", "Подключитесь к Mac", "Conecta tu Mac",
     "连接到 Mac"},
    {"Open OpenDisplay on your Mac, then", "Откройте OpenDisplay на Mac",
     "Abre OpenDisplay en tu Mac", "在 Mac 上打开 OpenDisplay"},
    {"select your Switch from the device list.",
     "и выберите свой Switch в списке.", "y selecciona tu Switch.",
     "然后选择你的 Switch。"},
    {"Both devices need the same Wi-Fi network.",
     "Подключите устройства к одной сети Wi-Fi.",
     "Usa la misma red Wi-Fi en ambos equipos.",
     "两台设备需连接同一 Wi-Fi 网络。"},
    {"Receiver address", "Адрес приёмника", "Dirección del receptor",
     "接收器地址"},
    {"Video decoder", "Декодер видео", "Decodificador de vídeo", "视频解码器"},
    {"Frame statistics", "Статистика кадров", "Estadísticas de vídeo",
     "帧统计"},
    {"Automatic", "Автоматически", "Automático", "自动"},
    {"On", "Вкл.", "Activadas", "开启"},
    {"Off", "Выкл.", "Desactivadas", "关闭"},
    {"Language", "Язык", "Idioma", "语言"},
    {"Either stick / D-pad: select", "Любой стик / крестовина: выбор",
     "Sticks / cruceta: seleccionar", "任一摇杆 / 方向键：选择"},
    {"Confirm", "Подтвердить", "Confirmar", "确认"},
    {"Y  Statistics", "Y  Статистика", "Y  Estadísticas", "Y  统计"},
    {"Disconnect", "Отключиться", "Desconectar", "断开连接"},
    {"-  Decoder", "-  Декодер", "-  Decodificador", "-  解码器"},
    {"Exit application", "Закрыть приложение", "Cerrar aplicación", "退出应用"},
    {"Back", "Назад", "Volver", "返回"},
    {"Press + to open settings during video",
     "Нажмите + для настроек при просмотре",
     "Pulsa + para abrir los ajustes durante el vídeo", "播放时按 + 打开设置"},
    {"IDLE", "ОЖИДАНИЕ", "EN ESPERA", "空闲"},
    {"STREAMING", "ТРАНСЛЯЦИЯ", "TRANSMITIENDO", "传输中"},
    {"DESKTOP IDLE", "ЭКРАН БЕЗ ИЗМЕНЕНИЙ", "ESCRITORIO SIN CAMBIOS",
     "桌面无变化"},
    {"DELAY", "ЗАДЕРЖКА", "RETARDO", "延迟"},
    {"Waiting for sender...", "Ожидание подключения Mac…", "Esperando al Mac…",
     "等待 Mac 连接…"},
    {"Software mode. Waiting for sender...", "Режим CPU. Ожидание Mac…",
     "Modo CPU. Esperando al Mac…", "CPU 模式。等待 Mac…"},
    {"NVDEC mode. Waiting for sender...", "Режим NVDEC. Ожидание Mac…",
     "Modo NVDEC. Esperando al Mac…", "NVDEC 模式。等待 Mac…"},
    {"Network changed. Waiting for sender...", "Сеть изменилась. Ожидание Mac…",
     "Red cambiada. Esperando al Mac…", "网络已更改。等待 Mac…"},
    {"H.264 decoder initialization failed.",
     "Не удалось запустить декодер H.264.",
     "No se pudo iniciar el decodificador H.264.", "无法启动 H.264 解码器。"},
    {"Connected. Waiting for H.264 video...", "Подключено. Ожидание видео…",
     "Conectado. Esperando vídeo…", "已连接。等待视频…"},
    {"Video stream connected", "Видеопоток подключён", "Vídeo conectado",
     "视频流已连接"},
    {"Menu", "Меню", "Menú", "菜单"},
    {"Settings", "Настройки", "Ajustes", "设置"}};
constexpr bool completeCatalog() {
  for (const auto &entry : strings)
    for (const auto *translation : entry)
      if (!translation || !*translation)
        return false;
  return true;
}
static_assert(completeCatalog(),
              "Every UI entry requires all four translations");
inline const char *text(Key key, Language language) {
  return strings[key][int(language)];
}
inline const char *name(Language language) {
  constexpr const char *names[] = {"English", "Русский", "Español", "简体中文"};
  return names[int(language)];
}
inline const char *status(const char *value, Language language) {
  if (!std::strcmp(value, "WAITING FOR MAC"))
    return text(Waiting, language);
  for (int i = Waiting; i < Count; ++i)
    if (!std::strcmp(value, strings[i][0]))
      return strings[i][int(language)];
  return value;
}
} // namespace locale
