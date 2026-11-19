#ifndef COWSTAT_H
#define COWSTAT_H

// Estadísticas de memoria y de Copy-on-Write que devuelve cowstat().
struct cowstat {
  uint64 freepages;   // páginas físicas libres en todo el sistema
  uint64 sharedpages; // páginas físicas con más de una referencia
  uint64 cowfaults;   // fallos CoW atendidos para el proceso que llama
  uint64 cowcopies;   // de esos fallos, cuántos copiaron la página
};

#endif
