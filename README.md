# Super Mario 64 (PS1 Port) 🇮🇹

Questo repository è un fork del progetto di [de-compilazione completa di Super Mario 64](https://github.com/n64decomp/sm64). È stato fortemente modificato per rimuovere il target Nintendo 64 e mirare esclusivamente all'hardware di **Sony PlayStation (PSX / PS1)**.

---

## Modifiche, Fix e Ottimizzazioni Sviluppate

In questa versione sono stati risolti diversi colli di bottiglia critici per permettere l'esecuzione stabile sia su emulatore che su **hardware originale (PS1)**:

### 1. Ottimizzazione del Grafo di Rendering (`node_rendered.c` / Engine Graph)
* **Riscrittura del Walk-Graph dei Nodi:** La gestione originale del rendering basata sul percorso dei nodi (`node_rendered.c` / `gfx_dimensions` / render walker) è stata alleggerita per l'architettura MIPS/GTE della PS1.
* **Riduzione dell'Overhead in Memoria:** Eliminati i calcoli ridondanti in virgola mobile nei nodi di trasformazione, sostituendoli con chiamate dirette a matrici a punto fisso (Fixed-Point Math) compatibili con la GTE.
* **Pulizia del Loop di Passaggio dei Livelli:** Ridotta la frammentazione della RAM durante l'attraversamento e il rendering dell'albero dei nodi grafici, prevenendo i crash di saturazione della memoria (OOM su 2MB RAM) quando si entra nelle mappe di gioco.

### 2. Driver CD-ROM e Gestione I/O (`cd_psx.c`)
* **Risoluzione Deadlock su Disco Reale:** Corretto il ciclo di attesa degli interrupt nel driver CD bare-metal. In precedenza, in caso di errori di lettura (`INT5`) o riposizionamento della lente (*seek retry*), il sistema entrava in un loop bloccante che faceva spegnere la rotazione del motore CD.
* **Sistema di Retry e Timeout sui Settori:** Inserito un sistema di riprovazione automatica (`CD_MAX_RETRIES`) e timeout sul segnale `DRQ` (Data Request) e DMA.
* **Ricerca Difensiva nel File System ISO-9660:** Aggiunta la gestione a tentativi multipli durante la lettura del Volume Descriptor per la localizzazione di `EXT.DAT;1`.

---

## Caratteristiche Principali (Features)

* **Supporto DUALSHOCK™:** Implementata la grafica "DUAL SHOCK™ Compatible" con supporto alla vibrazione analogica per il motore grande del controller.
* **Matematica Fixed-Point Personalizzata:** Convertiti i vettori a 16-bit e le matrici per sfruttare appieno l'hardware grafico della PS1.
* **JIT Display Lists & Tessellazione:** Conversione al volo delle Display List dell'RSP N64 in una struttura più compatta con tessellazione dei poligoni estesi (fino a 2x) per attenuare il flickering della GPU PS1.
* **Compressione Animazioni in VRAM:** Le animazioni di Mario sono compresse (da ~580 KB a ~190 KB) e allocate in una sezione protetta della VRAM per evitare letture da CD durante l'esecuzione del frame.
* **Encoder Texture a 4-bit (16 Colori):** Quantizzazione automatica di tutte le texture per rientrare nella memoria VRAM.

---

## Problemi Noti (Known Issues)

* **Limiti RAM (2 MB Retail):** Alcuni livelli o sequenze animate particolarmente dense potrebbero saturare i 2 MB di RAM standard. Su emulatore si raccomanda di abilitare l'opzione **8 MB RAM**.
* **Tracce Audio:** La musica di sottofondo richiede l'estrazione manuale delle tracce in formato `.wav` dentro la cartella `.local/` prima di avviare la compilazione.
* **Distorsione Texture:** Assenza di correzione prospettica hardware sulla GPU della PS1, che può causare un leggero effetto "wobble" o distorsione sulle superfici ampie.

---
