# La Vida Misma: especificacion ejecutable de diseno

Esta es la fuente breve de verdad para el comportamiento canonico. Las afirmaciones
en presente deben corresponder a codigo, configuracion o pruebas; los modelos
historicos y la fundamentacion extensa permanecen en `doc/plans/` y
`doc/bases_matematicas/`, pero no reemplazan al ejecutable.

## 1. Premisa y limite del modelo

**Implementado.** La simulacion ocurre en una grilla 2D generada por WFC. Sus
habitantes despiertan dentro de una fabrica heredada: no la fundan, no reciben
ordenes individuales y dependen materialmente de mantener un flujo de output. La
politica canonica trata a la fabrica como una institucion indiferente, no como un
adversario que reconoce identidades o interpreta actos culturales
(`Grid::generate_wfc()` en `src/grid.h`, `Simulation` en `src/simulation.cpp` y
`external.policy_variant = 1` en `config/default.toml`).

**Objetivo.** Hacer observable la tension entre sostener una institucion necesaria
y dedicar tiempo a supervivencia, relaciones, expresion, descanso y proposito, sin
convertir esa tension en un guion ni en control directo del jugador.

**Hipotesis por validar.** La presion material retardada debe cambiar la asignacion
colectiva de tiempo sin requerir castigos morales, enemigos identificados o una
semilla que garantice supervivencia y cultura.

## 2. Institucion, cuota y soporte externo

**Implementado.** Solo el `OUTPUT` retirado de Storage dentro de radio Manhattan 3
del Exit cuenta como enviado. En NORMAL, el cumplimiento actualiza una EMA y una
curva suave:

```text
fill_t    = clamp(output_enviado_t / demanda_t, 0, 1)
support_t = EMA(fill_t, response_ticks)
supply_t  = floor + (1 - floor) * smoothstep(low, high, support_t)
```

Los valores canonicos son respuesta 600 ticks, suelo 0.20 y umbrales 0.05/0.45.
`supply_t` escala unicamente la regeneracion posterior de FoodSource y ScrapPile;
no modifica utilidad, estres, eficiencia ni mortalidad. La condicion fabril es un
agregado mecanico, no una salud moral. CALM fija demanda cero y soporte uno. La
politica institucional canonica solo usa desgaste/carga de conveyors, ocupacion
anonima de Storage y conteos espaciales agregados. La regla de sobreocupacion lee
unicamente `alive` y posicion para contar cuerpos, no identidad, personalidad,
acciones, targets, confianza, comunidades, utilidad ni output estrategico
(`Simulation::system_ship_out_food()` y
`system_update_factory_condition()` en `src/simulation.cpp`, `src/sim_policy.cpp`,
`src/sim_space_policy.cpp`, y `test_external_supply_causality` en
`tests/simulation_tests.cpp`). Las variantes `external.supply_variant = 0` y
`external.policy_variant = 0` son controles historicos A/B, no el modelo canonico.

**Objetivo.** Representar una dependencia material, gradual y recuperable: fallar
envios reduce reposicion futura; restaurarlos mejora primero soporte, despues
stocks y finalmente riesgo de supervivencia. La fabrica nunca mata mediante una
orden directa.

**Hipotesis por validar.** El efecto debe mantenerse en muestras multisemilla y
horizontes largos sin que el suelo de suministro vuelva irrelevante la cuota ni
que topologias concretas dominen el resultado.

## 3. Fabrica heredada y flujo fisico

**Implementado.** Cada mapa comienza, antes de crear la poblacion, con una cadena
minima alcanzable y degradada de tres maquinas:

```text
FoodSource -> FoodMachine -> FOOD
ScrapPile  -> MaterialsMachine -> CONSTRUCTION_MATERIAL
CONSTRUCTION_MATERIAL -> OutputMachine -> OUTPUT -> Storage cercano a Exit -> envio
```

WFC coloca una FoodMachine, una MaterialsMachine, una OutputMachine, tres Storages
y cuatro conveyors con condiciones distintas; la OutputMachine queda conectada al
Exit. Los habitantes pueden operar, mantener, transportar, reparar y ampliar esa
infraestructura. Conveyors y agentes son rutas fisicas de transporte y cada belt
lleva un solo tipo de recurso. Construir puede desactivarse sin eliminar la cadena
inicial (`actions.allow_build` en `config/default.toml`,
`Grid::minimum_chain_present()` en
`src/grid.h`, `src/sim_conveyor.cpp`, y las pruebas
`test_inherited_factory_map_properties`, `test_inherited_chain_operates_without_build`
y `test_output_haul_requires_storage_arrival` en `tests/simulation_tests.cpp`).
FoodSource y ScrapPile son depositos del mapa cuya reposicion depende del soporte;
las materias primas no aparecen como inputs magicos en los bordes. Entrance se usa
para llegadas de personas, no para recursos.

**Objetivo.** Conseguir que producir, transportar y enviar sean hechos distintos y
auditables. Output atrapado en maquina, inventory, conveyor o Storage remoto no
satisface la cuota.

**Hipotesis por validar.** La cadena degradada debe crear cuellos de botella
reparables y decisiones logisticas legibles sin convertir BUILD en un acto
fundacional ni imponer una plantilla central de trabajadores.

## 4. Habitantes y decision individual

**Implementado.** Cada habitante es una entidad ECS con necesidades de hambre,
descanso, social, expresion, proposito y significado; enfermedad; personalidad
continua; cuatro skills; inventario; estres/trauma; estado social; memoria de
lugares y ciclo vital (`src/components.h`). Hay trece acciones, incluido `IDLE`.
Cada accion registra `UtilityBreakdown {self, factory, cost, risk, final,
feasible}`. La seleccion canonica es Boltzmann sobre acciones factibles con
`stress.selection_temperature = 0.4`; una utilidad cero recibe peso cero e `IDLE`
es siempre una alternativa real. El valor cero de temperatura es solo el limite
greedy, no el comportamiento por defecto (`Simulation::system_compute_utility()`
en `src/sim_utility.cpp`, `action_feasible()` en `src/sim_targets.cpp`, y
`test_metrics_contract`/`test_build_can_be_disabled` en
`tests/simulation_tests.cpp`).

La urgencia de supervivencia usa la variante sigmoide 3 y el estres canonico usa
modificadores continuos; `StressState` es una etiqueta de presentacion. Los skills
progresan solo cuando una accion tiene efecto y aumentan suavemente utilidad y
eficacia; no existe olvido. REST no requiere cama y CREATE no consume un recurso de
arte: una unidad creativa requiere por defecto veinte ticks efectivos y produce un
artefacto (`urgency.curve_variant = 3`, `urgency.stress_model_variant = 1` y
`culture.creative_work_ticks = 20` en `config/default.toml`).

La mayor parte de la utilidad y los targets productivos observa estado dentro de
radio Manhattan 12 y no consulta `ProductionChain::assess()`. Un target recordado
puede quedar fuera del radio, pero sus propiedades actuales remotas no se vuelven
a leer. Quedan dos limites conocidos a la localidad estricta: el planner de
conveyors todavia escanea topologia fabril global antes de comprobar visibilidad,
y A*, despues de escoger un target visible o recordado, usa el mapa completo para
encontrar una ruta y cachearla
(`Simulation::OBSERVATION_RADIUS`, `find_preferred_place()` en
`src/sim_targets.cpp`, `cached_next_step()` en `src/sim_movement.cpp`, y
`test_unseen_stock_does_not_change_decision`).

**Objetivo.** Hacer que la conducta se explique por estado individual,
observaciones, memoria, relaciones y azar privado, sin reparto por ID, profesion
impuesta ni omnisciencia economica.

**Hipotesis por validar.** La retroalimentacion practica-skill-eficacia-utilidad
debe producir especializacion persistente mas alla de la correlacion directamente
inducida por los coeficientes de personalidad.

## 5. Mecanismos sociales, culturales y espaciales

**Implementado.** Familiaridad y confianza son aristas dirigidas continuas. La
copresencia aumenta familiaridad; WORK, BUILD y CREATE efectivos compartidos
refuerzan relaciones; recibir ayuda aumenta confianza hacia quien ayuda; observar
conflicto cambia `observador -> actor`; el reparto de comida depende de excedente,
hambre, distancia, confianza, gregariousness e influencia (`src/social.h`,
`Simulation::system_social_learning()` y `src/sim_execute.cpp`). `community_id` se
deriva periodicamente del grafo solo para observacion y metricas; no concede
utilidad, comida, significado, proteccion ni privilegios, restriccion cubierta por
`tests/verify_policy_audit.cmake` y `test_graph_labels_are_behavior_neutral`.

Cada agente recuerda hasta 24 experiencias de lugar. REST, SOCIALIZE y CREATE
puntuan lugares por afinidad aprendida, distancia, trafico observable, ruido,
riesgo, comida, personas conocidas y artefactos. EatingZone no otorga una ventaja
categorica y CREATE funciona sobre suelo ordinario. Los artefactos tienen una
respuesta de mood modulada por artistry y pueden desactivarse de forma
contrafactual (`PlaceMemoryComponent` en `src/components.h`,
`find_preferred_place()` en `src/sim_targets.cpp`, `system_spatial_learning()` en
`src/simulation.cpp`, `culture.*` en `config/default.toml`, y
`test_create_completes_discrete_work_units_on_ordinary_floor`).

**Objetivo.** Permitir coordinacion, comunidades observadas y apropiacion de
lugares como resultados de interacciones continuas, no como efectos de una etiqueta
de faccion, arquetipo, zona cultural o modelo Schelling codificado.

**Hipotesis por validar.** Segregacion espacial, subculturas artisticas, liderazgo
causal, free-riding, huelgas y tradiciones requieren contrafactuales y evidencia
multisemilla adicional; no se consideran implementados por la mera existencia de
clusters, influencia o correlaciones trait-accion.

## 6. Ciclo vital e historia

**Implementado.** Los IDs son historicos, monotonicos y no se reutilizan; las
entidades muertas permanecen para analisis y liberan claims fisicos. Hay mortalidad
natural, llegadas exogenas por Entrance y reproduccion. Los intentos de llegada
dependen de tick y seed, no de muertes ni de una poblacion objetivo: un intento sin
capacidad se pierde. La reproduccion evalua cada 50 ticks condiciones continuas de
edad, proximidad, seguridad alimentaria local, necesidades, estres, mood,
familiaridad y confianza. Un descendiente hereda el promedio de seis traits con
mutacion acotada `+-0.08`, pero no skills, XP, arquetipo, comunidad, relaciones,
opiniones, memoria ni progreso creativo (`src/sim_lifecycle.cpp`, `[lifecycle]` en
`config/default.toml`, y las pruebas
`test_arrivals_are_exogenous_and_newcomers_start_empty`,
`test_reproduction_inherits_traits_not_roles_or_relationships` y
`test_dynamic_identity_and_ten_thousand_tick_turnover`). Todas las muertes pasan
por el pipeline exclusivo `Simulation::kill_agent()` y aplican duelo una vez.

**Objetivo.** Producir cohortes, genealogia e integracion sin reemplazo forzado y
sin predestinar roles o grupos a nuevas personas.

**Hipotesis por validar.** Que existan generaciones no demuestra transmision
cultural. Debe medirse si relaciones, convivencia, opiniones y objetos persistentes
transmiten patrones entre cohortes mejor que controles barajados o desactivados.

## 7. Director humano y replay

**Implementado.** `DirectorCommand` es una variante tipada que solo permite fijar
cuota, capacidad anonima de ocupacion, colocar o retirar estructura y priorizar
mantenimiento. La API no acepta identidad, accion, target conductual,
personalidad, relacion ni utilidad (`src/director.h`, `src/sim_director.cpp` y la
auditoria `tests/verify_policy_audit.cmake`). Cada intervencion aceptada registra
tick y secuencia antes de `Simulation::advance()`. La GUI puede grabar TOML schema
2 con seed, modo y huella FNV-1a de la fuente de configuracion; `vida_batch replay`
rechaza diferencias y reproduce el ledger. El estado resultante se compara en
`test_director_log_round_trip_and_replay` y la CLI byte-identica en
`tests/verify_replay.cmake`. La salida JSON de replay conserva schema 1 y las
metricas usan schema 3. La vista normal muestra consecuencias observables y la
vista `F12` separa informacion de debug.

**Objetivo.** Dar agencia ambiental reproducible al jugador sin convertir al
Director en un despachador de personas ni asumir que su vista debug es conocimiento
del mundo.

**Hipotesis por validar.** Las cinco intervenciones fisicas deben ofrecer decisiones
jugables suficientes; cualquier comando nuevo debe conservar el limite tipado y
tener round-trip/replay determinista.

## 8. Pipeline de tick

**Implementado.** El orden efectivo es el de `Simulation::advance()` en
`src/simulation.cpp`; las intervenciones Director del tick se aplican antes de
entrar en esta secuencia:

```text
1. regenerar recursos; decaer necesidades y enfermedad
2. fijar/escalar cuota segun modo
3. calcular utilidad; buscar targets; mover; ejecutar acciones
4. aprendizaje social; aprendizaje espacial
5. transportar conveyors; enviar output y actualizar soporte
6. evaluar globalmente la cadena para diagnostico post-tick
7. aplicar presion institucional no-CALM y actualizar condicion fisica
8. efectos de artefactos; detectar comunidades; actualizar estres; muertes y duelo
9. contagio, influencia, mood, decay de relaciones y consecuencias de dismantle
10. metricas de emergencia; Chronicle; metricas de muerte; ciclo vital
11. incrementar contadores de tick
```

`ProductionChain::assess()` corre despues de acciones y shipping; su snapshot
global no participa en la utilidad o targeting canonicos. Chronicle clasifica
hechos por `EventType` y renderizar narrativa no consume RNG conductual
(`test_narrative_is_behavior_neutral`).

**Objetivo.** Mantener una causalidad temporal unica para batch y GUI, distinguir
seleccion, target, llegada y efecto, y hacer reproducibles las intervenciones antes
del tick.

**Hipotesis por validar.** Cualquier reordenamiento debe demostrar mediante replay
y metricas que no introduce feedback en el mismo tick donde el modelo especifica
retardo, en especial `shipping_t -> regeneracion_t+1`.

## 9. Evidencia, resultados negativos y criterio de afirmacion

**Implementado.** `vida_batch metrics` emite un unico JSON schema 3 con poblacion,
demografia, fabrica, funnel y descomposicion de acciones, recursos, maquinas,
stocks, red social, emergencia, necesidades, skills, eventos y timeline
(`src/batch_main.cpp`, `src/metrics.h` y `tests/verify_metrics.cmake`). La misma seed
y el mismo build producen salida identica; CTest cubre contrato schema 3, replay,
limites de politica/Director y fixtures deterministas (`tests/simulation_tests.cpp`).

La evidencia registrada hasta Fase 8 sostiene afirmaciones acotadas:

- Bloquear Exit a 3000 ticks redujo vivos en 14/20 seeds y aumento muertes por hambre en
  18/20; reabrirlo mejoro soporte en 5/5 y vivos en 4/5, con un empate. La secuencia
  observada fue envio, soporte, reposicion, stock y riesgo posterior
  (`doc/plans/2026-07-21-alineacion-diseno-implementacion.md`).
- En CALM, 20 seeds emparejadas a 1000 ticks dieron modularidad/estabilidad
  `0.443/0.672` con el modelo completo y `0/0` sin aprendizaje social. Sin afinidad,
  la persistencia espacial media bajo de `0.238` a `0.198`. Esto apoya componentes
  comunitarios derivados y memoria espacial, no segregacion.
- En CALM a 10000 ticks para seeds `0,1,2,3,7`, hubo picos de 51-64 personas,
  63-71 identidades historicas, 8-10 cohortes, generaciones maximas 2-3 y ningun
  fundador vivo. Esto verifica turnover y genealogia, no herencia cultural.
- La regresion de cierre de Fase 8 a 3000 ticks termino NORMAL con
  `51,48,51,49,50` vivos y cero target failures para seeds `0,1,2,3,7`.

Tambien se conservan resultados negativos. El delta de distancia frente a traits
barajados carece de intervalo estadistico y horizonte largo, por lo que no se
declara segregacion. No hay preferencia estetica compartida modelada, asi que no se
declara subcultura. No se midio precedencia causal de influencia, asi que no se
declara liderazgo. La correlacion contribucion-beneficio fue positiva (`0.693`) y
no demuestra free-riding. Desactivar efectos de artefactos no valido un efecto
cultural macro. Sustituir el kink de compliance cuando `meaning > 0.7` por una
sigmoide colapso una seed (`44 -> 10` vivos) y se revirtio; no todo umbral es un
parche (`doc/plans/2026-07-21-emergence-redesign.md`).

**Objetivo.** Reservar las palabras "emerge", "segregacion", "subcultura",
"liderazgo" y "free-riding" para diferencias reproducibles frente a un
contrafactual definido antes del experimento.

**Hipotesis por validar.** Faltan intervalos multisemilla y horizontes largos para
patrones espaciales, causalidad de influencia, distribucion contribucion-beneficio
y transmision entre generaciones. Un resultado nulo debe conservarse como
evidencia, no corregirse agregando un gate para producir el relato esperado.
