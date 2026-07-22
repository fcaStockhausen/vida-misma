# La Vida Misma: roadmap de madurez

La Fase 9 consolido documentacion y evidencia. El ejecutable y sus fuentes de
verdad describen el ciclo completo; este roadmap no promete features por antiguedad
ni usa conteos volatiles de lineas o archivos.

## Estado por fase

| Fase | Resultado | Estado |
|---:|---|---|
| 0 | Build headless, CTest, metricas deterministas y baseline | Completada |
| 1 | Semantica factual: muertes exclusivas, contadores y Chronicle tipado | Completada |
| 2 | Soporte material retardado a partir de output enviado | Completada |
| 3 | Politica fabril fisica e indiferente | Completada |
| 4 | Cadena heredada alcanzable de Food, Materials y Output | Completada |
| 5 | Percepcion local, acciones factibles y seleccion Boltzmann | Completada |
| 6 | Relaciones, comunidades observadas y afinidad espacial continuas | Completada |
| 7 | Mortalidad natural, llegadas exogenas, reproduccion y schema 3 | Completada |
| 8 | Director ambiental tipado, grabacion, replay y vista debug separada | Completada |
| 9 | Consolidacion documental y retiro de modelos obsoletos | Completada |

La especificacion canonica es `doc/design_spec.md`; el registro experimental y las
puertas de salida estan en
`doc/plans/2026-07-21-alineacion-diseno-implementacion.md`.

## Invariantes canonicos

1. La fabrica no lee identidad, personalidad, accion, relacion o comunidad para
   decidir presion institucional (`src/sim_policy.cpp`,
   `tests/verify_policy_audit.cmake`).
2. La fabrica no mata directamente. Hambre, agotamiento, breakdown, suicidio y
   mortalidad natural pasan por un pipeline exclusivo (`Simulation::kill_agent()`).
3. Solo output enviado desde Storage cercano a Exit cuenta; output producido o
   almacenado en otro lugar no satisface demanda.
4. El envio modula con retardo solo la reposicion material canonica; no concede
   utilidad, eficiencia ni castigo instantaneo (`external.supply_variant = 1`).
5. La poblacion hereda una cadena degradada de tres maquinas antes del primer tick;
   BUILD mantiene, adapta o amplia, pero no funda la fabrica.
6. Las decisiones usan seleccion Boltzmann entre acciones factibles, `IDLE`
   incluido; no hay reparto por ID ni argmax canonico
   (`stress.selection_temperature = 0.4`).
7. La observacion conductual tiene radio 12 salvo el planner global de conectividad
   de conveyors. A* tambien puede leer el mapa completo despues de elegir un target
   visible o recordado.
8. Comunidades, influencia, arquetipos y estados de estres son observaciones o
   entradas continuas, nunca privilegios colectivos discretos.
9. Las llegadas son exogenas y no reemplazan muertes; descendientes no heredan
   profesion, comunidad, skills ni relaciones.
10. El Director modifica cuota y entorno mediante comandos tipados; nunca ordena
    una persona. Sus eventos se aplican antes de `Simulation::advance()`.
11. CALM elimina cuota y presion institucional, pero conserva necesidades,
    relaciones, cultura y demografia configurada.
12. Una afirmacion emergente requiere contrafactual multisemilla; una correlacion
    inducida por utilidad no basta.

## Hallazgos verificados

- El bloqueo de Exit produjo contraccion material: a 3000 ticks redujo vivos en
  14/20 seeds y aumento muertes por hambre en 18/20. La reapertura elevo soporte
  antes que stocks, y mejoro supervivencia en 4/5 seeds con un empate.
- La fabrica heredada existe, es determinista, alcanzable y opera FOOD,
  CONSTRUCTION_MATERIAL, OUTPUT y shipping sin BUILD en los fixtures de
  `tests/simulation_tests.cpp`.
- Stock fuera del radio observable no cambia utilidad, accion ni target; las
  regresiones de Fases 5-8 cerraron con cero target failures.
- En el A/B CALM de 20 seeds, quitar aprendizaje social llevo
  modularidad/estabilidad de `0.443/0.672` a `0/0`; quitar afinidad redujo
  persistencia espacial de `0.238` a `0.198`.
- En cinco corridas CALM de 10000 ticks, ninguna retuvo fundadores; hubo 8-10
  cohortes, generaciones maximas 2-3 y poblacion capaz de crecer o caer sin
  correccion hacia el valor inicial.
- Replay de Director reproduce metricas, grid, poblacion, cuota, Chronicle y ledger;
  la CLI exige salida byte-identica y rechaza otra seed o huella de configuracion
  (`tests/verify_replay.cmake`).
- `vida_batch metrics` schema 3 cierra contabilidad historica y expone toggles
  contrafactuales, genealogia, cohortes, emergencia y funnel de acciones
  (`tests/verify_metrics.cmake`).

## Resultados negativos y preguntas abiertas

- No esta validada la segregacion espacial: el delta frente a traits barajados no
  tiene intervalo estadistico ni persistencia de largo plazo. No se implementa ni
  se reclama un modelo Schelling.
- No esta validada una subcultura artistica: no existe preferencia estetica
  compartida y la ablacion de artefactos no demostro un efecto cultural macro.
- No esta validado liderazgo causal: `influence` es continua, pero falta demostrar
  precedencia sobre coordinacion o movilidad.
- No esta validado free-riding: la correlacion contribucion-beneficio observada fue
  positiva (`0.693`), no evidencia de explotacion.
- Tener descendientes y cohortes no valida transmision cultural entre generaciones;
  schema 3 verifica genealogia y turnover, no tradicion.
- Reemplazar por sigmoide el kink de compliance en crisis de significado produjo
  colapso en una seed y se revirtio. Los gates se retiran solo tras A/B.
- Los resultados siguen siendo sensibles al layout; ninguna seed aislada acepta o
  rechaza un cambio de simulacion.

## Cierre de Fase 9

- Las fuentes academicas y `doc/design_spec.md` distinguen implementacion, objetivo
  e hipotesis.
- El plan adversarial queda como registro historico y la politica indiferente es la
  interpretacion canonica.
- HTML y PDF se regeneraron desde las fuentes corregidas.
- La auditoria final vinculo afirmaciones presentes con codigo, configuracion o
  tests, y conservo resultados colectivos no validados como preguntas abiertas.

## Trabajo posterior

1. Diseñar, antes de implementar nuevos mecanismos, experimentos largos con 20 o
   mas seeds para segregacion, liderazgo, free-riding y transmision cultural.
2. Tratar resultados nulos como cierres informativos de hipotesis, no como una
   obligacion de fabricar la conducta mediante gates.
