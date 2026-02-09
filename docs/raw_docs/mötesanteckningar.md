Mötesanteckningar - Sunspots
Arbetsmöte 16  januari

Deltagare: Tim, Jimmy, Emil, Patrik

* Samtliga överens om problemet som ska lösas
* Jimmy har gjort research på linear solver, den matematiska beräkningsmodell som kan användas för att optimera en variabel. I vårat fall, SEK
* Jimmy/ Tim: Output blir en JSON energi plan (Vi ska optiomera Kr)
* Jimmy: Om vi skapar output i JSON-format så kan vi också redovisa enkelt i dashboards


* Frågor att besvara: "zooma in" på varje låda, förtydliga ansvarsområde samt bryt ner i mindre moduler. 
* När detta är gjort kan vi fördela ansvars mellan oss och påbörja utveckling
* Förslag scrum master: Tim, diskuteras på måndag.
* Jimmy: förslag på att vi använder Doxygen 

INPUTS

* Väder Prognos 24 framåt
* Elpris 24h framåt

OUTPUT

* JSON Energi plan (vad ska va på och hur länge per kvart 24h fram)

Quarter | Net battery | Net grid |
1       | -3.2        | +4.1     |
2       | -3.2        | +4.1     |
3       | +3.45       | +1       |
4       | +4          | 0        |

/v1/lps
/v1/naive
/v1/health
/v1/rawInData


VARIABLER

KW/h
El ut

* användning
* ladda batteri

El in

* discharge
* solar
* grid

Kr

* sälja el
* köpa el

/**
 * @Param:
 * @Return:
