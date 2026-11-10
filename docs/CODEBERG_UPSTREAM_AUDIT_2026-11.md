# Verifica degli aggiornamenti Codeberg (10 novembre 2026)

**Origine:** https://codeberg.org/malucart/sm64-psx  
**Repository originale GitHub, archiviato:** https://github.com/malucard/sm64-psx  
**Fork di destinazione:** https://github.com/RonyxDumb/sm64-psx (branch `main`)

## Punto di confronto

- Ultimo commit del progetto originale su GitHub: `b8d1c550a0d2dd925f5f14fc410bd9106e2e3ca1` (5 agosto 2026).
- Ultimo commit disponibile su Codeberg durante l'audit: `342982ba8292840595ccab9058e7d01cf892ef14` (4 settembre 2026).
- **10 commit** di Codeberg successivi all'ultimo commit GitHub. Cronologia recuperata direttamente tramite checkout Git in GitHub Actions e confronto degli oggetti Git con le SHA dei file, non dedotta dai soli messaggi.

## Tutti i commit successivi alla migrazione

| Commit | Data | Descrizione upstream | Stato nel fork |
| --- | --- | --- | --- |
| `84ffbc8` | 2026-08-06 | README e Makefile | Cambiamenti fondamentali già presenti; Makefile PSX differisce per supporto SFX V4/EXT.DAT |
| `5e6b0d9` | 2026-08-27 | Correzioni build, ctags | Fix principali già presenti |
| `eda644f` | 2026-08-28 | Correzione texture a due canali | Integrato: `tools/convert_image_psx.c` e `src/game/main.h` identici |
| `b418061` | 2026-09-01 | Corruzione delle animazioni | Integrato: `src/game/memory.c` identico |
| `383c568` | 2026-09-01 | Riscrittura del renderer | Largamente integrata; renderer PSX e preprocessore del fork hanno correzioni aggiuntive |
| `37b7d7b` | 2026-09-01 | Sistemazione rimozione skybox | Regole di base integrate; il fork ha gestione skybox PSX aggiuntiva in EXT.DAT |
| `74a10a9` | 2026-09-01 | Corruzione delle texture | Integrato: `src/port/psx/gfx_texture_psx.c` e `tools/convert_image_psx.c` identici |
| `0ccbc45` | 2026-09-03 | Fix di compilazione | Fix fondamentali già presenti; build PSX modificata per SFX |
| `551c8bb` | 2026-09-03 | Preparazione modifiche ulteriori | Modifiche condivise già presenti; modifiche grafiche specifiche divergenti |
| `342982b` | 2026-09-04 | Luci opzionali a 15 bit | **Non importate:** esperimento indicato come non funzionante anche nel messaggio dell'autore |

## Risultato del confronto dei file

Sono stati confrontati **69 percorsi cambiati** fra il commit di migrazione e l'ultimo commit Codeberg:

- **46 percorsi identici a livello di blob Git** nel fork attuale, fra cui le correzioni di animazioni, texture, memoria e vari file del renderer.
- **14 percorsi divergenti**, soprattutto perché il fork contiene modifiche aggiuntive e/o correzioni ai sorgenti upstream, compresi audio SFX V4, gestione skybox, rendering e costruzione `EXT.DAT`.
- **9 percorsi invariati nel fork rispetto al commit originale**: sette sono file rimossi dall'upstream (tra cui strumenti storici e il vecchio renderer tessellazione) e due (`include/PR/gbi.h`, `src/engine/geo_layout.c`) appartengono alla sperimentazione dell'illuminazione a 15 bit.

### Perché non è stato eseguito un merge automatico

Applicare indiscriminatamente i commit upstream sovrascriverebbe interventi più recenti del fork. Esempi:

- `src/port/psx/audio_psx.c`: il fork implementa un sequencer SFX V4 con dati in `EXT.DAT`, non presente nel file Codeberg.
- `src/game/skybox.c`: il fork gestisce header e caricamento selettivo da `EXT.DAT`.
- `src/port/psx/gfx_dl_exec_psx.c` e `tools/preprocess_graphics.py`: il fork contiene correzioni della tessellazione e del clipping assenti nell'upstream.
- `Makefile.pc.mk` di Codeberg contiene marcatori di conflitto Git `<<<<<<< Updated upstream` / `>>>>>>> Stashed changes`; il fork non li contiene.
- Il commit `342982b` introduce l'opzione `COLOR555`, esplicitamente problematica. Non conviene includerla senza test hardware e regressione del renderer.

**Esito:** i fix upstream importanti e verificabili risultano già presenti; nessun file di gameplay è stato sovrascritto soltanto per allineare gli hash. Questo è un audit di sorgenti e cronologia, non una conferma di corretto funzionamento del gioco su hardware reale o emulatore.

Audit riproducibile: [esecuzione GitHub Actions](https://github.com/RonyxDumb/sm64-psx/actions/runs/42874739083).
