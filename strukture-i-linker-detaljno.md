# Strukture podataka, format .o fajla i rad linkera

## 1. Strukture (in-memory, tokom asembliranja)

### 1.1 Tabela simbola

```cpp
enum class SymbolBind { LOCAL, GLOBAL };   // extern = GLOBAL + !defined, videti napomenu

struct Symbol {
    std::string name;         // ime simbola
    int32_t     value;        // offset u sekciji, ili apsolutna vrednost (za .equ)
    std::string section;      // ime sekcije kojoj pripada; "" ako nema (extern ili .equ)
    SymbolBind  bind;         // LOCAL ili GLOBAL
    bool        defined;      // da li je simbol stvarno definisan (labelom ili .equ)
    int         num;          // redni broj u konačnoj tabeli (za referencu iz relokacija)
};

std::unordered_map<std::string, Symbol> symtab;   // ključ = ime simbola
int nextSymNum = 1;   // 0 je rezervisano za UND (undefined) placeholder simbol
```

Napomene:
- **Extern nije poseban bind** — to je `bind = GLOBAL` i `defined = false` (isto kao u pravom
  ELF-u, gde `STB_GLOBAL` + `st_shndx = SHN_UNDEF` znači "definisan negde drugde").
- **`.equ` simbol** ima `defined = true`, `section = ""` — razlika između externa i equ NIJE
  u tome da li ima sekciju (oba nemaju), već u vrednosti `defined`.
- Section-simboli (jedan po sekciji, vrednost 0, ime = ime sekcije) su opcioni ali korisni —
  omogućuju relokacijama da referišu "početak sekcije" na isti način kao i običnu labelu.

### 1.2 Tabela sekcija

```cpp
struct Section {
    std::string          name;
    std::vector<uint8_t> data;    // sirovi sadržaj, raste tokom asembliranja
};

std::unordered_map<std::string, Section> sections;   // ključ = ime sekcije
std::vector<std::string> sectionOrder;                // redosled otvaranja (za serijalizaciju)
std::string currentSectionName;
```

`currentOffset` se ne čuva posebno — uvek je jednak `sections[currentSectionName].data.size()`.

### 1.3 Tabela za backpatching (privremena, NE ulazi u izlazni fajl)

```cpp
enum class FieldWidth { W32, W12_SIGNED };

struct BackpatchEntry {
    std::string name;         // simbol koji se čeka
    std::string section;      // sekcija u kojoj je mesto koje treba popuniti
    uint32_t    patchOffset;  // offset u sekciji gde treba upisati vrednost
    FieldWidth  width;
    int32_t     addend;       // dodatna konstanta (npr. kod [%reg + simbol + K])
};

std::unordered_multimap<std::string, BackpatchEntry> backpatchTable;
```

- `W32` se koristi za `.word simbol` (4 bajta sirovih podataka, van formata instrukcije).
- `W12_SIGNED` se koristi za `Disp` polje unutar 4-bajtne instrukcije (skokovi, `[reg+simbol]`,
  `ld $simbol`...).
- Kad se simbol definiše, sve zapise za njega odmah popuni i ukloni iz mape.
- Sve što ostane u mapi na kraju (`.end`/EOF) treba pretvoriti u relokacioni zapis (ako je
  simbol `.extern`) ili prijaviti kao grešku "nedefinisan simbol" (ako nije).

### 1.4 Relokacioni zapis (deo konačnog izlaza)

```cpp
enum class RelocType : uint8_t { R_32, R_PC12S };

struct RelocationEntry {
    int32_t    sectionIdx;   // indeks sekcije kojoj OVAJ zapis pripada (0-indeksiran red u
                              // section table-u; flat lista, bez grupisanja po sekciji)
    uint32_t   offset;       // offset UNUTAR TE sekcije gde se vrši popravka
    RelocType  type;         // R_32 (4 bajta, .word) ili R_PC12S (12-bitni signed, Disp polje)
    int32_t    symbolIdx;    // indeks simbola u symtab čija vrednost se upisuje
    int32_t    addend;       // dodatna konstanta koja se sabira uz vrednost simbola
};

std::vector<RelocationEntry> relocations;   // JEDNA ravna lista za CEO fajl
```

---

## 2. Format izlaznog fajla asemblera (.o) — binarni layout

```
┌─────────────────────────────────────────────┐
│ HEADER                                        │
│  magic        (4B)   npr. "SSOB"              │
│  version      (4B)                            │
│  numSymbols   (4B)                             │
│  numSections  (4B)                             │
│  numRelocs    (4B)                             │
│  strTabOffset (4B)                             │
│  strTabSize   (4B)                             │
└─────────────────────────────────────────────┘
┌─────────────────────────────────────────────┐
│ SYMBOL TABLE  (numSymbols zapisa, fiksne veličine) │
│  nameOffset  (4B) -> offset u string tabeli    │
│  value       (4B, signed)                      │
│  sectionIdx  (4B) -> indeks u section table-u; │
│                      -1 ako simbol nema sekciju │
│  bind        (1B) 0=LOCAL, 1=GLOBAL            │
│  defined     (1B) 0/1                          │
│  padding     (2B)                              │
└─────────────────────────────────────────────┘
┌─────────────────────────────────────────────┐
│ SECTION TABLE (numSections zapisa)             │
│  nameOffset  (4B) -> offset u string tabeli    │
│  size        (4B) -> veličina sirovog sadržaja │
│  (offset u bloku sirovih podataka se NE čuva   │
│   eksplicitno — računa se kao prefiksna suma   │
│   size-ova prethodnih sekcija, jer su upisane  │
│   tim redosledom)                              │
└─────────────────────────────────────────────┘
┌─────────────────────────────────────────────┐
│ RELOCATION TABLE (numRelocs zapisa, FLAT lista,│
│  bez grupisanja — svaki zapis sam nosi kojoj   │
│  sekciji pripada)                              │
│  sectionIdx  (4B) -> kojoj sekciji pripada     │
│  offset      (4B) -> offset unutar te sekcije  │
│  type        (1B) R_32 / R_PC12S               │
│  padding     (3B)                              │
│  symbolIdx   (4B) -> indeks u symbol table-u   │
│  addend      (4B, signed)                      │
└─────────────────────────────────────────────┘
┌─────────────────────────────────────────────┐
│ STRING TABLE (strTabSize bajtova)              │
│  niz null-terminated stringova:                │
│  "a\0my_section\0handler\0..."                 │
└─────────────────────────────────────────────┘
┌─────────────────────────────────────────────┐
│ SECTION DATA (sirovi bajtovi SVIH sekcija,     │
│  nadovezani jedan za drugim, u istom           │
│  redosledu kao u SECTION TABLE)                │
└─────────────────────────────────────────────┘
```

### Objašnjenje svakog polja

**HEADER**
- `magic` — konstanta koja identifikuje da je fajl ovog formata (opciona provera zdravog razuma
  pri učitavanju).
- `numSymbols`, `numSections`, `numRelocs` — broj zapisa u odgovarajućim tabelama; omogućavaju
  čitaocu da unapred zna koliko fiksnih zapisa treba pročitati, bez potrebe da traži terminator.
- `strTabOffset`, `strTabSize` — gde počinje i koliko je veliki blok stringova; sve `nameOffset`
  vrednosti u symtab/sectab su relativne u odnosu na početak ovog bloka.

**SYMBOL TABLE**
- `nameOffset` — pošto je ime simbola promenljive dužine, ne čuva se inline (to bi razbilo
  fiksnu veličinu zapisa) — čuva se samo offset u string tabeli gde ime počinje (null-terminated).
- `value` — offset unutar sekcije (za SECTION_REL simbole) ili apsolutna vrednost (za ABS/equ).
- `sectionIdx` — -1 znači "nema sekciju" (extern nedefinisan ili equ konstanta); razlika između
  ta dva slučaja se čita iz `defined` polja, ne iz `sectionIdx`.
- `bind`, `defined` — kombinacija ova dva polja (uz `sectionIdx`) daje sve četiri semantičke
  kategorije: lokalna labela, globalna labela, extern (nerazrešen), equ konstanta.

**SECTION TABLE**
- `nameOffset` — isto kao gore, ime sekcije je u string tabeli.
- `size` — veličina sirovog sadržaja te sekcije u bajtovima; koristi se i za agregaciju u
  linkeru i za određivanje gde u SECTION DATA bloku ta sekcija fizički počinje.

**RELOCATION TABLE**
- `sectionIdx` — sekcija kojoj OVAJ konkretan relokacioni zapis pripada (npr. `.text` ili
  `.data`); pošto je lista flat (svi zapisi jedni za drugim, bez odvajanja po sekcijama), ovo
  polje je jedini način da se zna kome zapis pripada.
- `offset` — mesto UNUTAR te sekcije (ne globalna adresa!) gde treba upisati popravljenu
  vrednost.
- `type` — određuje širinu/format upisa: `R_32` upisuje ceo 32-bitni broj (za `.word`), a
  `R_PC12S` upisuje samo 12 bita, signed, unutar `Disp` polja instrukcije.
- `symbolIdx` — koji simbol (iz symbol table-a) treba razrešiti da bi se dobila vrednost koja
  se upisuje.
- `addend` — konstanta koja se dodaje na vrednost simbola pre upisa (npr. kod `[%reg + sym + 4]`
  ili kad se u izrazu simbolu dodaje pomak).

**STRING TABLE** — jedan blob svih imena (simbola i sekcija), null-terminated, radi uštede
prostora i da bi zapisi u symtab/sectab mogli da imaju fiksnu veličinu.

**SECTION DATA** — sirovi bajtovi mašinskog koda i inicijalizovanih podataka. Ovo je ono što
linker na kraju kopira/agregira i u šta upisuje razrešene relokacije.

---

## 3. Kako se prave relokacioni zapisi — detaljno, korak po korak

Relokacija se pravi **tačno onda kada asembler treba da upiše vrednost simbola koju TRENUTNO
ne poznaje pouzdano kao konačnu.** Postoje dva različita razloga zbog kojih vrednost nije
"konačna" u trenutku asembliranja:

1. **Simbol je `.extern`** — definisan je u nekom drugom modulu, asembler nikad neće znati
   njegovu vrednost, to je posao linkera.
2. **Simbol JESTE definisan u istom fajlu, ali vrednost mora biti finalna (apsolutna) adresa**
   — a apsolutna adresa zavisi od toga gde će linker fizički smestiti sekciju (bazna adresa),
   što asembler ne zna (on samo zna offset unutar sekcije, ne i gde će sekcija završiti u
   memoriji).

Ovo je bitna razlika: **relokacija nije potrebna samo za externe** — potrebna je svuda gde
finalna vrednost zavisi od odluke linkera. Konkretno:

- `.word b` gde je `b` bilo lokalni bilo eksterni simbol → **UVEK** relokacija tipa `R_32`,
  jer `.word` po specifikaciji upisuje apsolutnu adresu simbola, a apsolutna adresa je poznata
  samo nakon linkovanja.
- `ld $b, %r1` (immediate vrednost simbola) → **UVEK** relokacija (tipa zavisno od toga da li
  MOD koji se koristi ograničava na 12 bita ili ne — provери tačan encoding), iz istog razloga.
- `jmp b` (skok na simbol) → relokacija tipa `R_PC12S`, jer je `Disp` polje 12-bitno.

### Konkretan primer

```asm
.section my_data
.global counter
counter:
    .word 0

.section my_code
    ld $counter, %r1     # linija X
```

Korak po korak za liniju `ld $counter, %r1`:

1. Parser prepozna oblik operanda `$counter` → immediate vrednost simbola.
2. Asembler traži `counter` u `symtab`. Nalazi ga (jer je definisan par linija ranije u istom
   fajlu, u sekciji `my_data`), **ali** — vrednost koju `symtab["counter"].value` sadrži je
   samo offset **unutar** `my_data` sekcije (npr. 0), **ne** finalna apsolutna adresa. Ona je
   nepoznata dok linker ne odluči gde `my_data` počinje u memoriji.
3. Zato asembler:
   - upisuje **placeholder vrednost (0, ili offset koji trenutno zna)** u `Disp` polje
     instrukcije koja se upravo emituje,
   - kreira `RelocationEntry`:
     ```
     sectionIdx = indeks sekcije "my_code" (jer je TU instrukcija koja se ispravlja)
     offset     = offset unutar my_code gde počinje ova instrukcija (npr. 0, ili gde je Disp polje)
     type       = R_PC12S (ili odgovarajući tip za ovaj MOD)
     symbolIdx  = indeks simbola "counter" u symtab
     addend     = 0
     ```
4. Ovaj zapis se dodaje u `relocations` listu i na kraju izlazi u `.o` fajl.

Da je `counter` bio `.extern` umesto lokalno definisan, **identičan** proces bi se desio —
jedina razlika je što u koraku 2 asembler ne bi ni pokušao da nađe offset (jer simbol nema
sekciju/vrednost), već bi odmah znao da mora relokacija, pošto je `defined == false`.

### Pravilo koje vredi upamtiti

> Relokacija se pravi svaki put kad upisuješ vrednost simbola u sadržaj sekcije, a ta vrednost
> zavisi od **apsolutne (finalne) adrese** — bez obzira da li je simbol lokalan ili eksteran.
> Backpatch tabela (privremena, in-memory) rešava samo **relativne, unutar-fajlovske** forward
> reference (npr. skok unapred na labelu u istom fajlu gde je i offset i sekcija već poznata i
> neće se menjati) — ali čak i tu, ako krajnji encoding zahteva apsolutnu adresu (a ne samo
> offset unutar iste sekcije), i lokalna, već poznata vrednost mora ići kroz relokaciju, jer
> "poznato u trenutku asembliranja" (offset unutar sekcije) nije isto što i "poznato kao
> apsolutna adresa" (to zna samo linker).

---

## 4. Šta linker radi sa ovim izlazom — detaljno

Linker prima jedan ili više `.o` fajlova (svaki u formatu iz sekcije 2) i radi sledeće, u
ovom redosledu:

### Korak 1 — Učitavanje i agregacija

Za svaki ulazni `.o` fajl (u redosledu navođenja na komandnoj liniji):
- pročita symtab, sectab, relokacije, string tabelu, sirove podatke,
- za svaku sekciju: ako sekcija tog imena **već postoji** u globalnoj (linker-internoj) tabeli
  sekcija (iz prethodnog ulaznog fajla), **nadoveže** sadržaj na kraj postojeće (agregacija);
  ako ne postoji, kreira novu,
- pri agregaciji, svi offseti simbola i relokacija koji su bili relativni na tu sekciju **u
  okviru tog jednog ulaznog fajla** moraju se pomeriti (dodati) za veličinu sekcije koja je već
  postojala pre nadovezivanja (jer se ta instanca sekcije ubacuje "iza" prethodne).

### Korak 2 — Provera višestrukih definicija

Za svaki `GLOBAL` simbol, provери da nije definisan (`defined == true`) u više od jednog
ulaznog fajla. Ako jeste → greška, sa imenom simbola u poruci.

### Korak 3 — Određivanje baznih adresa sekcija

- Ako je `-relocatable`: sve sekcije kreću od adrese 0 (redosledom kako su agregirane),
  `-place` opcije se ignorišu.
- Inače: sekcije navedene u `-place=ime@adresa` idu na tačno tu adresu; ostale se ređaju redom
  odmah iza sekcije koja je smeštena na najvišu adresu. Provери preklapanja → greška sa
  imenima sekcija koje se preklapaju.

### Korak 4 — Izračunavanje finalnih vrednosti simbola

Za svaki simbol: `finalValue = baseAddress[symbol.section] + symbol.value` (za SECTION_REL
simbole), ili samo `symbol.value` direktno (za ABS/equ simbole, koji ne zavise od pozicioniranja
sekcija).

### Korak 5 — Provera nerazrešenih simbola

Svaki simbol koji je `GLOBAL` i `defined == false` u SVIM ulaznim fajlovima (dakle niko ga nije
definisao) → greška "nerazrešen simbol", sa imenom u poruci. (Ovaj korak se **preskače** za
`-relocatable` izlaz — tamo je dozvoljeno da simbol ostane nerazrešen, jer izlaz može biti
ponovo ulaz u linker kasnije.)

### Korak 6 — Primena relokacija

Za svaki `RelocationEntry` iz svih ulaznih fajlova:
1. Nađi `finalValue` simbola na koji relokacija pokazuje (iz koraka 4).
2. Izračunaj `patchValue = finalValue + addend`.
3. Nađi tačno mesto: sekcija (`sectionIdx`, prevedeno kroz agregacioni pomak iz koraka 1) +
   `offset`.
4. Upiši `patchValue` na to mesto, poštujući `type`:
   - `R_32` → upiši svih 32 bita, little-endian, prepiši 4 bajta.
   - `R_PC12S` → provери da `patchValue` staje u 12-bitni signed opseg (-2048..2047); ako ne
     staje → greška; ako staje, upiši samo tih 12 bita u `Disp` polje instrukcije (ostatak
     instrukcije, OC/MOD/registri, ostaje netaknut).

### Korak 7 — Generisanje izlaza

---

## 5. Dva izlaza linkera — i kako se razlikuju

### `-relocatable`

**Šta radi:** generiše **novi `.o` fajl**, u **istom formatu** kao izlaz asemblera (sekcija 2)
— sa sopstvenom symtab/sectab/relokacijama/sirovim sadržajem. Sve sekcije su smeštene od
adrese 0 (kao da je ovo jedan veliki spojeni modul), `-place` opcije se ignorišu.

**Zašto relokacije OSTAJU u izlazu:** ovaj fajl može biti **ponovo** ulaz u linker (npr. kad
želiš da povežeš više `.o` fajlova u jedan veći "pred-povezani" modul, a taj modul kasnije
povežeš sa još nečim). Zato mora sačuvati sve relokacione zapise koji se odnose na simbole koji
**i dalje** nisu razrešeni (npr. `extern` koji nije nađen ni u ovoj grupi fajlova) — te
relokacije prosleđuje dalje, nepromenjene (osim pomaka offseta usled agregacije).

**Provera koja se radi:** samo višestruke definicije (korak 2). **Ne** provерava se
nerazrešenost simbola (korak 5 se preskače) i **ne** provерava se preklapanje sekcija (jer nema
`-place`, nema mogućnosti preklapanja pošto se sve samo nadovezuje redom).

### `-hex`

**Šta radi:** generiše **tekstualni memorijski image** — parove (adresa, sadržaj), spreman da
se direktno učita u memoriju emulatora. Ovo je **konačan** proizvod — ne može se dalje linkovati.

**Šta se dešava sa relokacijama:** **potpuno iščezavaju** iz izlaza — sve relokacije su već
**primenjene** (korak 6), sve adrese su **konačne, apsolutne** vrednosti. Izlaz je čist
memorijski sadržaj, nema više metapodataka (nema symtab, sectab, relokacija) — samo bajtovi na
adresama.

**Provera koja se radi:** SVE provere iz koraka 2, 3, 5 (višestruke definicije, preklapanje
sekcija uz `-place`, nerazrešeni simboli) — jer ovo je finalni izlaz, mora biti potpuno i
konzistentno rešen.

### Tabelarni pregled razlika

| | `-relocatable` | `-hex` |
|---|---|---|
| Format izlaza | isti kao `.o` (asembler format) | tekstualni memorijski image |
| Bazna adresa sekcija | uvek 0 | prema `-place` / redom |
| `-place` opcije | ignorišu se | primenjuju se |
| Relokacije u izlazu | DA, prenose se dalje | NE, sve već primenjene |
| Provera nerazrešenih simbola | NE (može ostati extern) | DA (mora sve biti razrešeno) |
| Provera preklapanja sekcija | NE (nema place, nema preklapanja) | DA |
| Može biti ulaz u linker ponovo | DA | NE (konačan proizvod za emulator) |
| Namena | međukorak, dalje linkovanje | ulaz emulatora |
