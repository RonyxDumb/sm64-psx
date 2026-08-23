# Impostazione e compilazione del progetto

Questa repository produce principalmente una versione PlayStation 1 di *Super Mario 64*. Il file `sm64.exe` nella directory PSX è un **PS-X EXE** per console/emulatori, non un eseguibile Windows.

## Prerequisiti legali e asset

1. Procurarsi legalmente una ROM statunitense originale di *Super Mario 64*.
2. Copiarla nella radice della repository con il nome esatto:

   ```text
   baserom.us.z64
   ```

   Gli asset estratti non sono inclusi nel repository e la build US è l'unica configurazione supportata dal port.
3. La musica di sottofondo è facoltativa. Per includerla, creare `.local/` nella radice e inserirvi i file WAV numerati `0.wav` … `37.wav`. Senza questi file, la build usa tracce vuote; gli effetti sonori rimangono disponibili.

## Ambiente consigliato: container

Il percorso più riproducibile è Docker o Podman. Da un terminale nella radice del progetto:

```sh
./idc make verify-retail
```

`idc` costruisce l'immagine del container e inoltra il comando a `make`. Richiede un terminale interattivo; su Windows, se il terminale o l'automazione non espone una TTY, usare un Dev Container in VS Code/Zed oppure eseguire direttamente il comando Docker equivalente.

Per entrare in una shell del container:

```sh
./idc
```

Poi eseguire:

```sh
make verify-retail
```

## Build nativa Linux

Una build nativa richiede Linux, Python 3, GCC o Clang per gli strumenti host, `xxd`, libpng, le librerie di FFmpeg, Meson e un toolchain `mipsel-none-elf-gcc` 15 o successivo. Devono inoltre essere disponibili gli strumenti usati per creare l'immagine CD.

Dopo aver installato i prerequisiti:

```sh
make verify-retail
```

## Build retail PS1 da 2 MiB

Il comando consigliato è:

```sh
make verify-retail
```

oppure, tramite il container:

```sh
./idc make verify-retail
```

Il target genera la build PSX e verifica automaticamente che:

- non siano attivi `BIG_RAM`, `DEBUG`, `SAFE` o `BENCH`;
- la BSS termini prima della riserva di stack del sistema con 2 MiB;
- lo stack linker-defined rimanga nell'intervallo RAM retail;
- `.dl_exec` resti nel budget da 4 KiB della instruction cache;
- ELF, PS-X EXE, ISO e CUE esistano e abbiano firme valide.

Non usare queste configurazioni per validare il limite retail da 2 MiB:

```sh
make DEBUG=1
make SAFE=1
make BENCH=1
make BIG_RAM=1
```

Esse cambiano il layout o le caratteristiche della build e richiedono più RAM.

## Diagnostica runtime retail

Per generare la stessa build da 2 MiB con misurazioni di alta marea (high-water mark), usare:

```sh
make RETAIL_DIAGNOSTICS=1 verify-retail
```

oppure:

```sh
./idc make RETAIL_DIAGNOSTICS=1 verify-retail
```

Questa opzione non abilita `BIG_RAM`, `DEBUG`, `SAFE` o `BENCH`. Premere **R2** durante il gioco per attivare l'overlay: oltre al profiler mostra memoria main/level disponibile e minimo storico, stack usato/libero, uso corrente e massimo dell'arena Global DL, uso dei packet pool GPU e un indicatore `OVERFLOW DL/PKT` se la diagnostica ha bloccato una scrittura oltre il buffer.

Le dimensioni fisse di pool, framebuffer e packet pool non vanno ridotte senza prima raccogliere dati conservativi su percorsi di gioco reali con questa configurazione.

## Output della build PSX

Con la configurazione US predefinita, i file si trovano in `build/us_psx/`:

| File | Uso |
| --- | --- |
| `sm64.elf` | ELF con simboli, utile per debug e analisi della mappa. |
| `sm64.exe` | PS-X EXE, caricabile da hardware/emulatori PS1; non è un programma Windows. |
| `sm64.iso` | Immagine CD del gioco. |
| `sm64.cue` | Descriptor dell'immagine CD, da preferire insieme alla ISO quando l'emulatore lo supporta. |
| `sm64.map` | Mappa del linker, usata da `verify-retail`. |

## Prova in PCSX-Redux

Per testare la conformità retail:

1. Configurare PCSX-Redux con **2 MiB di RAM**.
2. Disattivare dynarec e l'OpenGL GPU quando si usa il debugger.
3. Aprire `build/us_psx/sm64.cue` con **File → Open Disk Image**.
4. Caricare `build/us_psx/sm64.elf` con **File → Load Binary** per mantenere simboli e call stack.
5. Provare almeno titolo/file select, castello, una transizione di livello, una zona aperta, HUD/menu, geometria testurizzata ed effetti con alpha. Con `RETAIL_DIAGNOSTICS=1`, annotare l'headroom dell'overlay e verificare che non compaia `OVERFLOW DL/PKT`.

Una build che supera la verifica statica non sostituisce questa prova: stack e pool dinamici dipendono dal percorso di gioco.

## Build PC di debug

Il backend PC è separato dalla build PSX. Su Windows/MSYS2 richiede SDL3 e produce un eseguibile Windows:

```sh
make PC=1
```

Output:

```text
build/us_pc/sm64.exe
```

Questo file è l'eseguibile da avviare su Windows; non va confuso con `build/us_psx/sm64.exe`.

## Pulizia

```sh
make clean
```

rimuove `build/`. Per rimuovere anche gli strumenti host ricompilati:

```sh
make distclean
```
