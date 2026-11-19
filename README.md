# Proyecto de Sistemas Operativos
## Proyecto 4: Copy-on-Write y MiniDB (xv6)

### Integrantes

- Daniela Giraldo Salas
- Gisel Lorena Jaramillo
- Matías Gil

### Curso

Sistemas Operativos

### Semestre

2026-2

---

# Descripción

**Parte A – Copy-on-Write.** `fork()` ya no copia la memoria del padre. `uvmcopy()` mapea en el hijo las mismas páginas físicas, quita `PTE_W` a las que eran escribibles (en padre e hijo), les pone `PTE_COW` (bit 8, uno de los bits RSW de la PTE) y aumenta un contador de referencias por página física. Cuando alguno de los dos escribe, el hardware genera un page fault (`scause` 15), `usertrap()` llama a `cowfault()` y esta función da al proceso su propia copia (o, si ya es el único dueño, solo le devuelve `PTE_W`). `copyout()` también resuelve páginas CoW, porque el kernel escribe en memoria de usuario sin pasar por el hardware de usuario. `kfree()` solo devuelve la página a la lista libre cuando su contador llega a 0.

**Parte B – `cowstat()`.** Nueva llamada al sistema (número 25) que llena una `struct cowstat` con: páginas libres del sistema, páginas físicas compartidas (contador > 1), y los fallos CoW y copias CoW del proceso que llama (contadores en `struct proc`).

**Parte C.2 – `minidb snapshot M`.** El proceso hace `fork()`; el hijo escribe la tabla de cuentas en `snap.db` y el padre, mientras tanto, aplica M transferencias deterministas (semilla 4). Gracias a CoW el hijo ve la tabla tal como estaba en el `fork()`. Al final el padre espera al hijo, relee `snap.db`, verifica que su checksum sea el de antes del `fork()` y que el saldo total se conserve, y reporta cuántas páginas se tocaron y cuántas copias CoW hubo.

---

# Archivos modificados

**kernel/**

- `riscv.h`: se define `PTE_COW` (bit 8) para distinguir "no escribible por ahora (CoW)" de "nunca escribible".
- `cowstat.h` (nuevo): `struct cowstat { freepages, sharedpages, cowfaults, cowcopies }`, compartida por kernel y usuario.
- `kalloc.c`: arreglo `refs.count[]` con un contador por página física (indexado por `(pa-KERNBASE)/PGSIZE`) protegido por `refs.lock`; `kalloc()` deja el contador en 1; `kfree()` decrementa y solo libera si llega a 0; `freerange()` pone el contador en 1 antes de llamar a `kfree()` (si no, el primer decremento daría -1); funciones `krefinc`, `krefcount`, `kfreepages` (contador `nfree` bajo `kmem.lock`) y `ksharedpages`.
- `vm.c`: `uvmcopy()` comparte páginas en vez de copiarlas; nueva `cowfault()`; `copyout()` resuelve páginas CoW antes de escribir.
- `trap.c`: `usertrap()` atiende `scause == 15` con `cowfault()` antes de probar la asignación perezosa (`vmfault`).
- `proc.h` / `proc.c`: campos `cowfaults` y `cowcopies` por proceso, en cero en `allocproc()`.
- `defs.h`: prototipos de las funciones nuevas.
- `syscall.h`, `syscall.c`, `sysproc.c`: `SYS_cowstat` y `sys_cowstat()`.

**user/**

- `usys.pl`, `user.h`: entrada y prototipo de `cowstat()`.
- `minidb.c`: implementación de `snapshot` (Parte C.2) y sus funciones auxiliares.

**Makefile**: se agregan `_pgtbltest`, `_cowtest` y `_minidb` a `UPROGS`.

---

# Integración con xv6

1. Clonar xv6-riscv, hacer `git switch -c proyecto4 06aad25` y aplicar `git apply base_proyecto3.patch`.
2. Copiar los archivos entregados en sus rutas.
3. Copiar los archivos del docente: `user/cowtest.c`, `user/minidb.h`, `user/pgtbltest.c` y `user/usertests.c`.
4. Compilar con `make qemu CPUS=3`.
5. Ejecutar `cowtest`, `minidb consulta 3`, `minidb snapshot 2000`, `pgtbltest` y `usertests -q`.

---

# Respuestas a las preguntas

> Los valores de PTE de la pregunta 1 son los de la salida de ejemplo del docente; 

## 1. PTE antes y después de una escritura CoW

Primera página de la tabla: dirección virtual `0x6000`.

- **Antes** (hijo, sin escribir): `pte 0x21fd11d3`, `pa 0x87f44000`. Los 10 bits bajos son `0x1d3 = 01 1101 0011`:
  - V = 1, R = 1, **W = 0**, X = 0, U = 1, A = 1, D = 1, **COW = 1**.
  - Es una página de datos, legible, accesible desde usuario, sin permiso de escritura y marcada como CoW. Apunta a la misma página física que usa el padre.
- **Después** (el hijo escribió): `pte 0x21fb28d7`, `pa 0x87eca000`. Bits bajos `0x0d7 = 00 1101 0111`:
  - V = 1, R = 1, **W = 1**, X = 0, U = 1, A = 1, D = 1, **COW = 0**.

La dirección física cambió porque la escritura sobre una página sin `PTE_W` provocó el fault. Como el contador de la página valía 2 (padre e hijo), `cowfault()` pidió una página nueva con `kalloc()`, copió los 4096 bytes, puso en la PTE del hijo la dirección nueva con `PTE_W` y sin `PTE_COW`, y llamó a `kfree()` sobre la página vieja, cuyo contador bajó a 1. Así el hijo escribe en una copia privada y no altera lo que ve el padre.

**El padre:** su PTE no cambia en ese momento. Sigue apuntando a `0x87f44000`, sin `W` y con `COW`. La diferencia es que ahora el contador de esa página es 1. Cuando el padre escriba, tendrá su fault, `cowfault()` verá `krefcount == 1` y no copiará nada: solo le devuelve `PTE_W` y quita `PTE_COW`, conservando la misma dirección física.

## 2. Páginas sin PTE_W y USYSCALL en fork()

- **Código (sin `PTE_W`):** `uvmcopy()` solo convierte en CoW las páginas que tienen `PTE_W` (y `PTE_U`). Las de solo lectura se mapean tal cual en el hijo, con el mismo `pa` y los mismos flags, y solo se incrementa su contador de referencias (para que `kfree()` no las libere mientras alguien las use). No se marcan como CoW porque nunca debieron ser escribibles: si se marcaran, una escritura accidental al código pasaría por `cowfault()`, que la "arreglaría" dándole una copia escribible, y el programa ya no moriría. La prueba `solo_lectura` de `cowtest` exige que ese acceso mate al proceso. `cowfault()` además devuelve -1 si la PTE no tiene `PTE_COW`.
- **Página de guarda de la pila:** en xv6 `uvmclear()` solo le quita `PTE_U`, nada más. Como el usuario no puede acceder a ella, `uvmcopy()` no la convierte en CoW (solo lo hace con páginas `PTE_W` y `PTE_U`) y `cowfault()` rechaza cualquier PTE sin `PTE_U`. Un acceso a la guarda sigue terminando en `usertrap(): unexpected scause`, que mata al proceso, como debe ser.
- **USYSCALL:** vive por encima de `sz` (justo bajo `TRAPFRAME`) y `uvmcopy()` solo recorre `[0, sz)`, así que ni se toca. Cada proceso recibe la suya en `allocproc()`/`proc_pagetable()`, con su propio `pid`, y siempre de solo lectura. Si el hijo compartiera la página del padre, `ugetpid()` en el hijo leería el pid del padre. Por eso `ugetpid_hijo` de `cowtest` exige que sean distintos. La misma lógica aplica al trapframe.

## 3. Carrera en el conteo de referencias

Suponga una página con contador 2, compartida por el padre P y el hijo C, y que la operación `cnt--`/`cnt++` no está protegida (leer, calcular, escribir son tres pasos).

1. CPU 0: P hace `fork()` de un nuevo hijo G; `krefinc()` **lee** el contador: 2.
2. CPU 1: C termina y su `kfree()` **lee** el contador: 2 y escribe 1.
3. CPU 0 continúa con lo que leyó y **escribe** 3.

Interleaving concreto: (1) CPU 0 lee 2; (2) CPU 1 lee 2, calcula 1 y escribe 1; (3) CPU 0 calcula 3 y escribe 3. Si el orden de las escrituras es el inverso, el contador queda en **1**, pero los dueños reales son P y G (2). Cuando P termine, `kfree()` verá 0 y devolverá la página a la lista libre mientras G todavía la usa: `kalloc()` se la entrega a otro proceso y G lee y corrompe datos ajenos (**liberación prematura**). Si el contador queda en **3**, nunca llegará a 0 y la página se **pierde para siempre**. Lo mismo ocurre con dos `kfree()` simultáneos: ambos leen 2, ambos escriben 1, y nadie la libera.

Con `refs.lock`, cada lectura-modificación-escritura es atómica y la decisión "¿llegó a 0?" se toma bajo el mismo lock. Un solo lock global basta porque las operaciones son muy cortas y no hay otra estructura que haya que mantener consistente a la vez; la lista libre se protege con su propio `kmem.lock`.

## 4. Consistencia del snapshot

- **Checksum de antes del `fork()`:** después del `fork()` la tabla es compartida y de solo lectura (CoW). Cuando el padre modifica una página, el fault le da al **padre** una copia privada y el hijo conserva la página original, sin cambios. El hijo escribe `snap.db` leyendo sus propias páginas, que para él son una foto exacta del instante del `fork()`. Las escrituras del padre nunca llegan a esas páginas.
- **`copias_cow` < 64:** la tabla ocupa 64 páginas (262144 bytes). Con 2000 transferencias, solo se tocaron unas 30 páginas distintas (`paginas_tocadas`; confirmen el número en su corrida), y solo esas se copian: CoW copia por demanda, solo lo que se escribe. Un `fork()` clásico habría copiado las 64 siempre, aunque M fuera 0 (con `snapshot 0`, `copias_cow=0`).
- **Si el padre esperara al hijo antes de transferir:** cuando el hijo ya terminó, su `exit()` liberó sus referencias y el contador de cada página vuelve a 1. Cada fault del padre vería `krefcount == 1` y solo reactivaría `PTE_W` sin copiar nada: `copias_cow` sería 0 (aunque `fallos_cow` seguiría igual). Además, el snapshot ya estaría terminado, así que no habría concurrencia: se pierde el punto del ejemplo.

## 5. Aceleración y planificador

| K | ticks `CPUS=1` | ticks `CPUS=3` |
|---|---|---|
| 1 | | |
| 2 | | |
| 3 | | |
| 4 | | |
| 8 | | |

- **De 1 a 3 trabajadores con `CPUS=3`:** cada trabajador es un proceso distinto con un tercio de las cuentas. El planificador de xv6 recorre la tabla de procesos desde cada hart y toma cualquier proceso `RUNNABLE`, así que los tres trabajadores se reparten en las tres CPUs y corren en paralelo. Con CoW no se copió la tabla al hacer `fork()`, y los trabajadores solo la leen, de modo que no hay faults ni contención: el tiempo baja casi a 1/K.
- **De 3 a 8:** solo hay 3 CPUs. Los 8 procesos tienen que turnarse con Round Robin (cada uno recibe un quantum por cada timer interrupt, y el planificador pasa al siguiente `RUNNABLE`). El trabajo total es el mismo, y el tiempo total sigue siendo (trabajo total)/(3 CPUs); crear más procesos solo añade `fork()` y cambios de contexto.
- **`CPUS=1`:** hay un solo hart. Aunque haya K procesos, solo uno corre a la vez, y Round Robin los intercala. El trabajo total no se reduce y se suma el costo de los cambios de contexto y de crear procesos, así que los ticks son iguales o algo peores que con K=1.

---

# Resultados de las pruebas


| Suite | Resultado |
|---|---|
| `cowtest` | (11 de 11) |
| `minidb` | |
| `pgtbltest` | |
| `usertests -q` | |

---

# Decisiones de diseño

- **Contador de referencias en un arreglo global** indexado por número de página física, en vez de un campo por página o una estructura dinámica: la memoria física de xv6 es fija (`KERNBASE` a `PHYSTOP`) y el arreglo ocupa poco.
- **Un solo `refs.lock`** para todos los contadores. Es simple y la sección crítica es de unas pocas instrucciones. Con muchas CPUs sería un cuello de botella; se podrían usar locks por rango de páginas.
- **`kalloc()` deja el contador en 1 y `kfree()` decide con el contador**, para que el resto del kernel (que ya llama a `kfree()` por cada página) no necesite saber de CoW.
- **`PTE_COW` en un bit RSW** para distinguir páginas CoW de páginas realmente de solo lectura (código).
- **Si el contador es 1 en `cowfault()`, no se copia**: solo se devuelve `PTE_W`. Evita copiar páginas que ya son exclusivas del proceso.
- **Falta de memoria en la copia:** `cowfault()` devuelve -1, `usertrap()` mata el proceso con `setkilled()` y el kernel no entra en pánico.
- **`copyout()` maneja CoW por su cuenta**, porque el kernel escribe a memoria de usuario con su propio acceso y nunca provoca el page fault de usuario.
- **`cowstat()` cuenta fallos y copias por proceso** (en `struct proc`) y páginas libres/compartidas globalmente.

---

# Limitaciones conocidas

- `ksharedpages()` recorre todo el arreglo de contadores bajo el lock: es O(páginas) y detiene `kalloc()`/`kfree()` mientras tanto. Es aceptable para estadísticas, no para una ruta caliente.
- Un único lock para todos los contadores no escala a muchas CPUs.
- Una página compartida, marcada CoW, no puede usarse para otro mecanismo que necesite `PTE_W` (p. ej. tras `fork()` el kernel no puede escribirla directamente, solo a través de `copyout()`).

---

# Declaración de uso de IA

Se usó Claude (Anthropic) como apoyo para revisar la solución de las Partes A, B y C.2, y para redactar un borrador de este README. Los integrantes revisamos el código, lo ejecutaron y asumimos plena responsabilidad académica sobre el contenido entregado.
