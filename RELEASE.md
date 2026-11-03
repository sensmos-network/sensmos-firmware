# Wydanie firmware — procedura (obowiązkowa)

1. **Build** 5 targetów: `build-all.ps1 -Targets esp32,esp32s3,esp32s3-n16r8,esp32c3,esp32s3-lora`.
2. **Weryfikacja binów**: `verify-bins.js <FW_BASE>` (skill fw-release). Czerwone = nie wydawać.
3. **Archiwum** na serwerze: `backend/public/fw/archive/<wersja>/` (+ `<wersja>-loraN/esp32s3-lora.bin`), `deploy.ps1 -NoRestart`.
4. **BRAMKA: nowa wersja musi dalej przyjmować OTA** (decyzja 2026-11-03, bez wyjątków).
   Serwer prowadzi kółko na nodach zapisanych testerów: `nowa → poprzednia → nowa`, każdy etap ≥ 10 min
   online, limit 3 h na etap. Wersja jest `ready` dopiero po ≥ 3 pełnych kółkach i 0 porażkach; jedna porażka
   (nod cofnął się sam, nie wraca, nie przyjął drugiego OTA) blokuje wersję dla wszystkich.
   Powód: błąd w funkcji zawsze poprawimy następnym OTA, ale zepsute OTA w nowej wersji = flota utknięta na zawsze.
5. **Promocja** (`POST /v1/admin/fw/promote`) tylko dla `ready`: biny z archiwum stają się bieżącym OTA.
6. **Rollout falami — automat** (`backend/fale.ps1 start <wersja>`, od 2026-11-03): tylko wersja `ready`;
   fale 2 % → 10 % → 30 % → reszta nodów danego wariantu, fala ≥ 1 h, dalej gdy ≥ 70 % nodów fali zgłosi nową
   wersję. Auto-wstrzymanie: 1 nod wrócił na starą wersję, > 10 % nodów zamilkło po OTA, albo po 6 h < 70 %.
   Wznowienie / przerwanie tylko ręcznie (`fale.ps1 wznow|przerwij <nr>`, `stan`). Po ostatniej fali promocja sama.
   Nigdy cała flota naraz. Nod sam cofa się do poprzedniej partycji, gdy nowa wersja nie podniesie WS w 5 min.
7. Web-flasher (`sensmos.com/flash`) dopiero po promocji; manifesty w `backend/public/flash/`.
