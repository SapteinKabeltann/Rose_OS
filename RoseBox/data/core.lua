-- Core: minimal kjerne. Lastes ved boot, forblir i RAM.
-- Filystem/skjerm/input/BLE er initialisert i C++ (setup).
-- Laster launcher fra C ved første loop() for å spare stack/minne ved boot.
local load_launcher_failed_logged = false

function loop()
  if not _G.launcher then
    local ok = HAL.load_launcher()
    if not ok then
      if not load_launcher_failed_logged then
        load_launcher_failed_logged = true
        print("load_launcher failed (se Serial for årsak)")
      end
      return
    end
  end
  return _G.launcher.loop()
end
