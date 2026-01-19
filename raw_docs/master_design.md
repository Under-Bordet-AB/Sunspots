SUNSPOTS master designSystem för prognostisering och optimering av solel baserat på väder- och spotprisdata

Genom att kombinera väderdata med spotprisdata från elnätet kan systemet beräkna optimala tider för elförbrukning, batteriladdning och försäljning av överskottsproduktion.

Projektet ska genomföras med agilt arbetssätt.

frågor?

1. Hur orkestrerar vi de olika modulerna i servern?

Programmen

Gemensamma krav

* Kommunikation via Unix domain sockets, pipes eller shared memory
* Minst en del av systemet körs i separat process
* Felhantering för API-fel, timeout och ogiltig data
* Konfigurationsfiler för systeminställningar
* Möjlighet att starta och stoppa systemet kontrollerat
* Loggning av viktiga händelser

Klient

* CLI
* C++ (måste använda RAII och STL där lämpligt)
* hämta prognoser från servern
* visa spotprisdata
* visa beräknad energiplan

Server

Tech & arkitektur

* C11
* select () / poll() / epoll()
* Multi-threaded design med tydlig pipeline: fetch → parse → compute → cache
* Modulär arkitektur med tydliga ansvarsområdens
* Minst en del av systemet körs i separat process

Måste kunna

* Hämta väderdata: solinstrålning, molnighet, temperatur
* Beräkna förväntad solcellsproduktion per 15 minuter
* Lagra prognoser lokalt med cache och TTL (Time-To-Live)
* Hämta spotprisdata per 15 minuter
* Matcha spotprisdata mot solprognos
* Beräkna optimala tider för elförbrukning, energilagring och försäljning
* Systemet ska generera en tidsbaserad energiplan (24-72 timmar) som visar:
* När systemet bör köpa el från nätet
* När egen solproduktion ska användas direkt
* När batteri ska laddas respektive urladdas
* När försäljning av överskottsproduktion är gynnsam

Delar i servern

Modul: HÄMTA

* hämta extern data (väder elpris soltimmar etc)
* hämta intern data (solcells produktion, batteri status, historisk förbrukning)

Modul: BERÄKNA

* Räkna ut  energioptimerings profil så fort vi får ny data

Modul: SPARA

* DATABAS: indata, beräkningar, utdata
* LOGG: anslutningar, mottagen och skickad data

Modul: LEVERERA

* exponera data och uträkning via API

Innan leverans

Definiera och dokumentera prestanda

* Profilering av beräkningssteget med gprof eller perf
* Identifiering av flaskhalsar i koden
* Dokumenterad optimering med före/efter-mätningar

Leverabler

Vid kursens slut (vecka 12) ska följande levereras:


Kod och system

* Komplett källkod i Git-repository
* Makefile eller build-script för kompilering
* Fungerande server och klient
* Konfigurationsfiler med dokumentation
* Automatiserade tester

Dokumentation

* README med installationsinstruktioner
* Arkitekturdokumentation med diagram
* API-dokumentation för server-klient-kommunikation
* Profileringsrapport med före/efter-mätningar
* Individuell skriftlig reflektion (per student)

Presentation

* Muntlig presentation (15-20 minuter)
* Live-demonstration av systemet
* Presentation av profileringsresultat och optimeringar
* Diskussion av designval och lärdomar

Stretch Goals - Utökad funktionalitet

Följande funktionalitet är valfri och ger möjlighet för starkare grupper att utmana sig själva ytterligare.


Kategori
	Stretch Goals

Avancerad IPC
	Shared memory med POSIX semaforer för cache; Message queues för asynkron kommunikation; Multiplexad I/O med select() eller poll()

Optimeringslogik
	Avancerade optimeringsalgoritmer för energiplanering; Maskininlärningsbaserad prognos; Batterimodellering med laddcykler och degradering

Skalbarhet
	Dynamisk trådpool baserad på systemlast; Connection pooling för databas/API-anslutningar; Distribuerad cache med invalidering

Prestanda
	SIMD-optimeringar för numeriska beräkningar; Cache-vänlig dataorganisering; Zero-copy I/O för stora dataöverföringar

Simulering
	Lastsimulering för hushållsförbrukning; Solpanelsmodellering med orientering och skuggning; Ekonomisk simulering med olika elpriser




Tekniska tips och resurser

Utvecklingsmiljö

* Använd Linux (Ubuntu rekommenderas) för utveckling
* Kompilera med gcc/g++ och flaggorna -Wall -Wextra -pthread
* Använd Valgrind för minnesläckagedetektering
* Använd GDB för debugging av flertrådade program
* Versionshantera med Git från dag ett

Kodkvalitet

* Följ en konsekvent kodstandard
* Dokumentera funktioner och klasser med kommentarer
* Skriv modulär kod med tydliga gränssnitt
* Använd meningsfulla variabel- och funktionsnamn
* Undvik globala variabler - använd parametrar och structs

Vanliga fallgropar att undvika

* Race conditions vid delad data utan synkronisering
* Deadlocks vid inkonsekvent låsordning
* Minnesläckor vid glömd free/delete eller saknad RAII
* Zombieprocesser vid utebliven wait()
* Buffertöverskridning vid osäker stränghantering
* Otillräcklig felhantering vid systemanrop

