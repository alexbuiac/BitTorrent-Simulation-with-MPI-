# APD - Tema2 - Protocolul BitTorrent

## 1. Tracker

Întâi de toate, facem schimb de informații cu toți clienții. Primim informații 
despre toate fișierele din rețea: cine le deține, câte segmente au și care sunt 
hash-urile acestora.

După faza inițială, intrăm într-o buclă infinită în care așteptăm cereri de la 
clienți. Aceste cereri pot fi:
- cererea informațiilor despre un fișier de către un client care vrea să îl 
  descarce (lista de seederi, numărul de segmente și hash-urile);
- actualizarea listei de seederi la fiecare 10 segmente descărcate;
- semnalul de final trimis de thread-ul de download al unui client atunci când 
  a terminat de descărcat un fișier.

Când toate fișierele sunt descărcate de toți clienții, tracker-ul trimite un 
semnal de final thread-urilor de upload ale clienților pentru a le permite să 
se oprească și își termină și el execuția.

## 2. Client

În faza inițială, clientul își citește fișierul de intrare și strânge informații 
despre fișierele pe care le deține, apoi se conectează la tracker și îi trimite 
aceste informații.

După faza inițială, clientul așteaptă de la tracker un semnal că își poate 
începe execuția. După ce primește semnalul, clientul se împarte în două fire de 
execuție: unul pentru download și unul pentru upload.

### a) Download

Clientul ia pe rând toate fișierele pe care vrea să le descarce. Trimite o 
cerere inițială tracker-ului pentru a primi informațiile necesare descărcării. 
După ce primește răspunsul, începe să ceară segmente de la seeders aleși la 
întâmplare din lista de seederi. Pentru a evita ca un seeder să fie 
suprasolicitat, nu se trimit două cereri consecutive aceluiași seeder.

În funcție de răspunsul seeder-ului, clientul marchează segmentul ca descărcat 
sau continuă să trimită cereri pentru el. După finalizarea descărcării tuturor 
fișierelor, clientul trimite un semnal de final tracker-ului.

### b) Upload

În thread-ul de upload, clientul așteaptă cereri de la alți clienți. Pentru 
fiecare cerere, verifică dacă are segmentul cerut și răspunde în consecință. 

Acest thread se închide în momentul în care primește semnalul de final de la 
tracker.

## Detalii suplimentare

- Am folosit un mutex pentru a proteja accesul la lista de hash-uri a 
  segmentelor descărcate.
- Am folosit câte un tag diferit pentru fiecare pereche `send-recv`, întrucât 
  se încurcau adesea mesajele cu același tip de date trimise consecutiv.
