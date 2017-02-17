# cavacamisa
Tools per l'analisi del gioco di carte "cavacamisa"

Gli script python sono basati su https://github.com/matthewmayer/beggarmypython e adattati alla versione italiana con asso, 2 e 3.
* alltest2.py permette di controllare tutte le partite a partire da un mazzo preimpostato.
* single.py permette di vedere lo svolgimento di una partita fornendo come input le mani dei due giocatori.

Le versioni in C++ sono scritte da me.
Consiglio di usare l'opzione -O2 in fase di compilazione, per rendere più veloce l'esecuzione.
* cavacamisa2.cpp è l'analogo di alltest2.py.
* cavacamisa_multi.cpp è multithreaded.
* cavacamisa_multi_arr.cpp usa la struttura `mano`, che è molti più efficiente delle liste.
* cavacamisa_testone.cpp serve per vedere se una partita è infinita; stampa tutte le mosse.

## Partite infinite
... in corso ...
Nella cartella `infinite` si trovano le partite infinite che ho trovato, come output del programma testone.

2017/02/17: trovata la prima partita infinita della storia di cavacamisa.

