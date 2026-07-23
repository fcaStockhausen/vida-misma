# Plan de alineación entre diseño, filosofía e implementación

> **Estado (2026-07-22):** Fases 0 a 9 completadas y verificadas.
>
> **Decisión canónica:** la fábrica es una **institución indiferente**, no un
> oponente que identifica enemigos o interpreta culturalmente sus actos.
>
> **Relación con planes anteriores:** este plan reemplaza la dirección filosófica
> de `2026-05-30-factory-as-antagonist.md`, pero conserva sus mecanismos útiles de
> presión impersonal. `2026-07-21-emergence-redesign.md` sigue siendo el registro
> de los experimentos de utilidad ya completados.

## Objetivo

Alinear el ejecutable con la tesis central de La Vida Misma:

> Los habitantes despiertan dentro de una institución que no construyeron, no
> controlan y no comprende sus deseos. La fábrica exige output sin odiarlos ni
> conocerlos. Los habitantes necesitan mantener ese sistema porque de su
> funcionamiento dependen los recursos materiales que les permiten vivir, pero
> el tiempo dedicado a sostenerlo compite con sus necesidades humanas.

El objetivo no es hacer que toda conducta sea impredecible. Es separar con
claridad:

- reglas institucionales impersonales;
- decisiones individuales autónomas;
- fenómenos colectivos derivados de interacciones;
- interpretación narrativa, que no debe modificar la simulación.

## Invariantes canónicos

Estas condiciones son restricciones de diseño, no parámetros de balance:

1. **La fábrica no conoce identidades.** Ningún sistema fabril puede leer
   `agent_id`, arquetipo, facción, opinión, confianza, expresión, significado o
   no-conformidad para elegir a quién perjudicar.
2. **La fábrica no mata directamente.** Los agentes mueren por hambre,
   agotamiento, enfermedad, accidente o consecuencias sociales; nunca por una
   instrucción equivalente a `if factory_health == 0: die`.
3. **Solo el output enviado cuenta.** Producir output y dejarlo en una máquina no
   satisface a la institución. El vínculo material parte de lo efectivamente
   enviado por el Exit.
4. **La dependencia es material y retardada.** Fallar output reduce gradualmente
   el soporte externo o la reposición de recursos; no aplica daño moral ni un
   castigo instantáneo.
5. **Los habitantes heredan una fábrica operativa.** Debe existir una cadena
   mínima antes de que aparezca la población. Construir puede ampliar, reparar o
   adaptar el sistema, pero no constituye el acto fundacional de la colonia.
6. **Las etiquetas no causan conducta.** `community_id` es la única agrupación
   derivada actual y existe solo para análisis. “Facción”, “resistencia”, “líder”
   o “subcultura” son interpretaciones posibles que todavía requieren métricas;
   ninguna etiqueta concede inmunidad, recursos, utilidad o bonificaciones.
7. **Los agentes no reciben órdenes ocultas.** Sus decisiones no se reparten por
   `agent.id` y usan observacion local salvo dos excepciones tecnicas declaradas:
   conectividad global del planner de conveyors y A* global despues del target.
8. **La emergencia requiere contrafactuales.** Una correlación directamente
   inducida por un coeficiente de utilidad no basta para declarar que emergió una
   cultura, especialización, huelga o segregación.

## Diagnóstico inicial que motivó el plan

La tabla siguiente conserva el baseline previo a la Fase 0; no describe el estado
actual posterior a las Fases 1-8.

| Brecha | Estado al iniciar el plan | Resultado buscado |
|---|---|---|
| Dependencia output-supervivencia | Implementada mediante soporte EMA y reposición material; queda retirar deuda social en CALM | El output enviado sostiene gradualmente la reposición material |
| Fábrica indiferente | Política física canónica implementada; la estrategia anterior queda solo para A/B | Desgaste, cuotas y reestructuración basados solo en reglas físicas/institucionales |
| Fábrica preexistente | WFC coloca una cadena mínima heredada, degradada, alcanzable y ampliable | Cadena mínima heredada, degradada y ampliable |
| Autonomía local | Utilidad usa inventarios y necesidades globales; routing por `agent.id` | Percepción local, memoria limitada y señales observables |
| Espacios emergentes | EatingZone central preconstruida y targets explícitos | Afinidad espacial y congregación medibles sin zona obligatoria |
| Facciones/cultura | Umbrales y privilegios discretos | Estructura social continua; etiquetas solo analíticas |
| Especialización | XP afecta producción pero no elección; dos habilidades no progresan | Feedback suave entre práctica, habilidad, eficacia y preferencia |
| Ciclo vital | Cohorte inicial sin reposición ni generaciones | Llegadas, envejecimiento y herencia social configurables |
| Director humano | Solo modos de arranque | Intervención ambiental reproducible, nunca órdenes a agentes |
| Evidencia | Métricas parciales y algunos contadores incorrectos | Salida estructurada, tests deterministas y A/B multisemilla |

## Orden de ejecución

| Fase | Propósito | Dependencia | Estado |
|---|---|---|---|
| 0 | Contrato experimental y build headless | Ninguna | Completada |
| 1 | Corregir observabilidad y semántica existente | Fase 0 | Completada |
| 2 | Restaurar el vínculo material output-supervivencia | Fase 1 | Completada |
| 3 | Convertir la fábrica en una institución indiferente | Fase 2 | Completada |
| 4 | Hacer que la fábrica realmente preexista | Fase 3 | Completada |
| 5 | Localizar y simplificar la decisión individual | Fase 4 | Completada |
| 6 | Sustituir macroconductas etiquetadas por dinámica continua | Fase 5 | Completada |
| 7 | Añadir ciclo vital y generaciones | Fase 6 | Completada |
| 8 | Implementar el Director humano | Fases 2-7 | Completada |
| 9 | Consolidar documentación y retirar modelos obsoletos | Todas | Completada |

Cada fase debe cambiar un mecanismo causal principal. No combinar, por ejemplo,
la reducción de suministro con un rediseño simultáneo de utilidad o muerte.

## Fase 0 - Contrato experimental y build headless

### Meta

Poder verificar cambios sin SDL, sin inspección manual de texto y sin confundir
acciones seleccionadas con acciones realmente ejecutadas.

### Trabajo

1. Añadir una opción CMake que permita configurar y compilar `vida_batch` sin
   buscar SDL2 ni SDL2_ttf. La configuración GUI seguirá siendo explícita.
2. Incorporar CTest con un ejecutable pequeño de tests deterministas, sin nuevas
   dependencias externas.
3. Añadir `vida_batch metrics <ticks> <seed>` con una única salida JSON final.
4. Mantener acumuladores de demanda real, output enviado, acciones seleccionadas,
   acciones ejecutadas, fallos de target, muertes por causa, máquinas construidas,
   recursos producidos/consumidos y estado social agregado.
5. Capturar una línea base fresca de 3000 ticks para semillas `0 1 2 3 7` en
   NORMAL, CALM y PRODUCTION_TEST.
6. Reservar una batería de al menos 20 semillas para validar afirmaciones macro;
   las cinco semillas establecidas siguen siendo el ciclo rápido de regresión.

### Archivos probables

`CMakeLists.txt`, `src/batch_main.cpp`, `src/simulation.h`, `src/simulation.cpp`,
un nuevo `src/metrics.*` y un nuevo directorio `tests/`.

### Puerta de salida

- `vida_batch` configura sin SDL.
- La misma semilla y configuración producen JSON idéntico en ejecuciones
  consecutivas del mismo build.
- Las métricas distinguen selección, llegada al target y efecto ejecutado.
- Los tests pueden ejecutarse con `ctest --test-dir build --output-on-failure`.

### Resultado implementado (2026-07-21)

- `VIDA_BUILD_GUI=OFF` evita toda búsqueda de SDL para batch y tests.
- `vida_batch metrics <ticks> <seed> [normal|calm|production]` emite un único
  objeto JSON con demanda real, envío, funnel de acciones, recursos, muertes,
  máquinas y agregados sociales.
- `vida_tests` comprueba contrato de métricas y replay determinista del mismo build.
- `run 200 42` conserva exactamente el SHA-256 de `HEAD`, confirmando que la
  instrumentación no cambia la conducta observada.
- Dos ejecuciones de `metrics 200 42` producen JSON byte a byte idéntico.
- La compilación directa con GCC 16.1.0 y las revisiones fijadas de EnTT/tomlplusplus
  pasó sin warnings; `vida_tests` también pasó.
- Se capturó la línea base de Fase 0, previa a las correcciones causales, con
  3000 ticks para `0 1 2 3 7` en NORMAL, CALM y PRODUCTION_TEST.
- CMake 4.3.1 configuró el proyecto headless con Visual Studio 2026; `vida_batch`
  y `vida_tests` compilaron en Release y CTest pasó 2/2.

| Línea base F0 | seed 0 | seed 1 | seed 2 | seed 3 | seed 7 |
|---|---:|---:|---:|---:|---:|
| NORMAL: vivos / output enviado | 45 / 151.0 | 22 / 163.7 | 46 / 333.2 | 43 / 345.9 | 44 / 202.5 |
| CALM: vivos / output enviado | 8 / 0.0 | 25 / 0.0 | 30 / 0.0 | 45 / 0.0 | 43 / 0.0 |
| PRODUCTION_TEST: vivos / output enviado | 45 / 86.7 | 28 / 196.6 | 9 / 147.5 | 15 / 102.3 | 34 / 145.7 |

## Fase 1 - Corregir observabilidad y semántica existente

### Meta

Eliminar errores que invalidan experimentos antes de tocar el modelo filosófico.

### Trabajo

1. Ejecutar `system_hidden_space_exposure()` una sola vez y nunca en CALM.
2. Hacer mutuamente excluyentes las causas de muerte; una entidad no puede morir
   dos veces ni generar duelo duplicado en el mismo tick.
3. Integrar suicidio y breakdown en el mismo pipeline factual de muerte y duelo.
4. Corregir `total_machines_built_` para las tres rutas dinámicas de construcción.
5. Clasificar eventos por tipo explícito en el lugar de emisión, no por búsqueda
   frágil de frases como `"BUILT a machine"` o `"BREAKDOWN"`.
6. Calcular la cuota media desde la demanda acumulada por `Simulation`, usando la
   misma escalada y cap que el runtime.
7. Resolver knobs engañosos: implementar o eliminar `simulation.use_wfc`, rangos
   de personalidad sin uso y `portion_size` sin efecto.
8. Corregir comentarios obsoletos en `config/default.toml` como parte del mismo
   cambio que corrige su comportamiento.

### Tests mínimos

- CALM no genera deterioro, reestructuración, Watcher ni sellado de espacios.
- Una muerte produce una causa, un evento y una aplicación de duelo.
- Construir cada tipo de máquina incrementa exactamente un contador.
- La demanda acumulada del batch coincide con la aplicada por `Simulation`.
- Chronicle serializa hechos aunque cambie el texto narrativo asociado.

### Puerta de salida

Las cinco semillas conservan su conducta salvo por efectos explicables de los
bugs corregidos, y todos los contadores estructurados cuadran con el estado ECS.

### Resultado implementado (2026-07-21)

- Hidden-space exposure se ejecuta una vez por tick en NORMAL y nunca en CALM.
- CALM no ejecuta Watcher reports.
- Todas las causas terminales pasan por una única operación: una causa, un evento
  factual, una métrica y una aplicación de duelo por agente.
- Suicidio usa el mismo pipeline y breakdown se registra como `DIED_BREAKDOWN`.
- `FIRST_DEATH` reconoce starvation, exhaustion, breakdown, collapse y suicide.
- Las rutas FoodMachine, MaterialsMachine y OutputMachine actualizan el contador
  histórico y las métricas estructuradas exactamente una vez por finalización.
- La cuota media usa demanda y output acumulados por `Simulation`, incluida la
  escalada y su cap real.
- `emit_log` exige `EventType`; se eliminó la clasificación por substrings y se
  añadieron tipos factuales para storage, frames, eating zones y enfermedad.
- Chronicle usa un RNG de presentación local derivado del evento. Renderizar un
  journal ya no consume RNG conductual ni comparte un `TextGen*` entre mundos.
- Se eliminaron `use_wfc`, `portion_size` y los rangos de personalidad porque se
  parseaban sin afectar el ejecutable; `default.toml` quedó sincronizado.
- CTest cubre las tres rutas de máquina, cap de cuota, muerte exclusiva, duelo,
  suicidio, CALM, replay, neutralidad narrativa y contrato JSON.

| Modo | seed 0 | seed 1 | seed 2 | seed 3 | seed 7 |
|---|---:|---:|---:|---:|---:|
| NORMAL: vivos F0 -> F1 | 45 -> 45 | 22 -> 22 | 46 -> 46 | 43 -> 43 | 44 -> 44 |
| CALM: vivos F0 -> F1 | 8 -> 8 | 25 -> 39 | 30 -> 30 | 45 -> 45 | 43 -> 41 |
| PRODUCTION_TEST: vivos F0 -> F1 | 45 -> 45 | 28 -> 28 | 9 -> 9 | 15 -> 15 | 34 -> 34 |

NORMAL y PRODUCTION_TEST conservaron también el output enviado exactamente. En
CALM, los Watcher reports pasaron a cero en las cinco semillas. La sanción por
comer junto a una máquina sigue afectando temporalmente `factory_health` en CALM:
retirarla aquí causó extinción en seed 0, por lo que se mantiene como mecanismo
aislado hasta rediseñarla en la Fase 3 junto con toda la política indiferente.

## Fase 2 - Vínculo material entre output y supervivencia

### Meta

Hacer verdadera la tesis “si la fábrica se detiene, la vida se vuelve
materialmente inviable” sin daño directo ni castigo instantáneo.

### Modelo propuesto

Usar una única señal continua basada en output **enviado**:

```text
fill_t    = clamp(shipped_t / demanded_t, 0, 1)
support_t = EMA(fill_t, response_time)
supply_t  = floor + (1 - floor) * smoothstep(low, high, support_t)
```

`supply_t` representa soporte externo: energía, repuestos y reposición de
insumos. El primer experimento debe aplicarlo solo a la regeneración de recursos.
No multiplicar simultáneamente regeneración, eficiencia, utilidad, estrés y
mortalidad; eso recrearía la pila de parches que el rediseño anterior eliminó.

### Trabajo

1. Añadir demanda acumulada, EMA de cumplimiento y `external_supply_factor` a
   `Simulation` y `Config`.
2. Hacer que solo `system_ship_out_food()` actualice cumplimiento institucional.
3. Retirar la curación directa de `factory_health_` desde FoodMachine y
   OutputMachine. Output almacenado o atrapado no tiene valor institucional.
4. Escalar primero la reposición de FoodSource/ScrapPile mediante
   `external_supply_factor`, con un suelo bajo que permita recuperación.
5. Añadir retardo y suavizado suficientes para que un fallo breve no cause una
   espiral inmediata, pero un fallo sostenido agote existencias.
6. Redefinir `factory_health_` como condición mecánica agregada de infraestructura
   o retirarlo si duplica machine/conveyor condition. No usarlo como marcador
   moral de obediencia.
7. Conservar las muertes existentes por necesidades; no crear `FACTORY_DEATH`.

### Experimentos A/B

| Variante | Intervención | Predicción |
|---|---|---|
| Control | Cadena normal | La colonia sostiene suministro y supervivencia en la mayoría de semillas |
| Sin envío | Bloquear Exit, no la producción | Cae soporte, luego stocks, después supervivencia |
| Output atrapado | Producir pero impedir transporte | Se comporta como incumplimiento, demostrando que solo enviar cuenta |
| Recuperación | Reabrir Exit tras una crisis | El suministro y parte de la población pueden recuperarse |
| CALM | Sin cuota ni contracción | El soporte permanece neutral; no existe presión fabril |

### Puerta de salida

- En las cinco semillas, el control no pierde más de 30% de vivos respecto de su
  línea base salvo justificación explícita.
- Bloquear envíos empeora materialmente supervivencia en la mayoría de al menos
  20 semillas, con hambre/agotamiento como causas mediadoras.
- Ninguna rama de código mata por salud fabril o cuota.
- Restaurar envíos mejora soporte antes de mejorar stocks y supervivencia; el
  orden causal aparece en las métricas.

### Resultado implementado (2026-07-21)

- `external.supply_variant = 1` es el default; variante `0` conserva el modelo
  histórico de presión por salud para comparación A/B.
- `system_ship_out_food()` es el único escritor runtime de `external_support` y
  `external_supply_factor`. La EMA parte de 1, responde en 600 ticks y usa una
  curva smoothstep con suelo 0.20, umbral bajo 0.05 y alto 0.45.
- El factor afecta exclusivamente la reposición de FoodSource y ScrapPile. Como
  regeneración ocurre antes de shipping, el fill del tick `t` actúa desde `t+1`.
- En variante 1, cuota y salud no amplifican utilidad, estrés, mortalidad,
  eficiencia ni roturas. Los boosts de `last_quota_fill` y el planner reciben una
  señal neutral. Las necesidades físicas y logística de output permanecen.
- FoodMachine y OutputMachine ya no curan salud por producir en NORMAL o
  PRODUCTION_TEST. Output almacenado, atrapado o diagonalmente fuera del radio
  Manhattan del Exit no mejora soporte.
- `factory_health` pasa a observar la condición media de infraestructura
  completada; frames sin construir no cuentan y la métrica no causa conducta.
- `metrics` acepta ventanas end-exclusive de bloqueo/reapertura, sampling de
  timeline y override de reestructuración. El JSON incluye parámetros, soporte,
  factor, reposición base/solicitada/real, stocks y bloqueo efectivo.
- CALM fija demanda a 0 y soporte/factor a 1. Conserva temporalmente la semántica
  de salud de Fase 1 para no mezclar esta fase con la deuda social de Fase 3.

El A/B de bloqueo se ejecutó con reestructuración deshabilitada para no confundir
la contracción material con targeting estratégico. A 3000 ticks, sobre seeds
`0..19`, bloquear Exit redujo vivos en 14/20 y aumentó muertes por hambre en
18/20. Dos seeds sin cadena funcional (`5` y `16`) no distinguieron la
intervención. No apareció una causa de muerte fabril nueva.

| seed | base F1 | control F2 | Exit bloqueado |
|---:|---:|---:|---:|
| 0 | 45 | 39 | 42 |
| 1 | 22 | 41 | 28 |
| 2 | 46 | 41 | 38 |
| 3 | 43 | 43 | 44 |
| 7 | 44 | 43 | 38 |

Ningún control de la suite establecida perdió más de 13.3% respecto de Fase 1.
La comparación macro, no cada seed aislada, sostiene el efecto del bloqueo.

Para recuperación se bloqueó `[1500,3000)` y se comparó a tick 4500 contra un
bloqueo permanente desde 1500:

| seed | reabierto | bloqueo permanente |
|---:|---:|---:|
| 0 | 37 | 37 |
| 1 | 40 | 38 |
| 2 | 33 | 32 |
| 3 | 46 | 44 |
| 7 | 41 | 30 |

El soporte final fue mayor en 5/5 y los vivos en 4/5, con un empate. En seed 7,
soporte/factor subieron de `0.053/0.200` en tick 3000 a `0.199/0.449` en 3100;
el stock de comida siguió cayendo hasta tick 3500 y empezó a recuperarse en 3600.
Esto observa el orden envío -> soporte -> reposición -> stock -> riesgo futuro.
CALM conservó exactamente los vivos de Fase 1 (`8,39,30,45,41`) y soporte 1.

## Fase 3 - Fábrica indiferente

### Meta

Eliminar interpretación social de la política fabril. La institución puede ser
dura, creciente y absurda; no puede saber quién se resiste.

### Trabajo

1. Sustituir el targeting de reestructuración basado en facciones por una regla
   ciega: desgaste, carga mecánica, antigüedad o selección uniforme reproducible.
2. Retirar de la política canónica `faction_target_bonus`, Foreman reports y toda
   lectura de opinión, confianza, facción o no-conformidad.
3. Reemplazar Watcher por interacción social local: un agente puede desaprobar un
   acto que presencia según sus propias opiniones, pero esa desaprobación no
   informa ni activa a la fábrica.
4. Eliminar la pérdida mágica de salud por comer junto a una máquina. Si comer en
   el puesto tiene costo, debe ser físico: interrupción, contaminación o riesgo,
   aplicable aunque nadie lo vea.
5. Convertir el sellado de espacios en una regla institucional neutral basada en
   ocupación, seguridad o mantenimiento, sin saber que el lugar es cultural.
6. Mantener cuota, desgaste y eventos exógenos solo si obedecen reglas estables y
   no seleccionan símbolos o grupos.
7. Conservar temporalmente la política estratégica detrás de una variante A/B
   para comparación histórica; no será el default ni la explicación canónica.

### Tests e inspección estática

- Los sistemas fabriles no leen `SocialFabric`, `FactionComponent`, opiniones,
  personalidad ni identificadores de agente.
- Dos mundos con el mismo estado físico y distinta red social reciben la misma
  distribución de eventos fabriles.
- Un acto no-productivo no cambia salud o suministro por su significado.
- CALM produce cero eventos institucionales de presión.

### Puerta de salida

La presión sigue siendo observable, pero `analysis` ya no puede producir una
métrica llamada “restructures vs factions” porque esa causalidad dejó de existir.

### Resultado implementado (2026-07-21)

- `external.policy_variant = 1` es canónico. Variante `0` conserva completa la
  política estratégica, Watcher, no-conformidad y sanciones históricas para A/B.
- La política canónica vive en `sim_policy.cpp`; un hash estable derivado de seed,
  época y posición decide gate, jitter y target sin consumir RNG conductual.
- Los únicos targets canónicos son conveyors construidos por desgaste/carga y
  Storage no vacío por ocupación total. No se leen máquinas, tipo de recurso,
  output, Exit, identidades ni estado social.
- Un evento reduce condición de conveyor en 0.15 sin cruzar el suelo reparable
  0.20, o elimina 10% de cada recurso almacenado con la misma fracción. Toda
  pérdida queda en `resources_lost`.
- Se conserva un draw de compatibilidad en cada época para evitar que separar el
  RNG institucional desplace gratuitamente el stream conductual cuando el gate
  legacy no dispara. La selección y el efecto canónicos siguen siendo estables.
- Watcher, clasificación de acciones como no-conformes, escudo de facción y
  estrés por no-conformidad no se ejecutan en variante 1.
- Comer cerca de una máquina no altera salud, soporte ni estrés institucional.
  El testigo local con mayor ética de trabajo puede desaprobar al agente mediante
  una relación de confianza; esa interacción no emite reportes fabriles.
- El cierre de espacios usa `occupancy_capacity` y `overcapacity_ticks` en una
  unidad separada. No consulta `HiddenSpace`, cultura o identidad y no inyecta
  estrés directo. CALM no ejecuta cierres ni milestones de cuota/crisis.
- `analysis` solo imprime targeting de facciones y Foreman reports al solicitar
  explícitamente policy `0`; el análisis canónico no contiene esa métrica.
- Se corrigió una pérdida silenciosa en conveyors: al depositar parcialmente en
  Storage ahora se descuenta solo la cantidad aceptada, no todo el throughput.

CTest incluye una inspección estática de ambas unidades de política, fixtures de
conveyor y Storage, cierre anónimo, comida observada, variante legacy y veinte
pares por seed con física idéntica y redes sociales opuestas. Los veinte pares
producen el mismo evento y estado físico canónicos.

| seed | vivos legacy | vivos canónicos | enviado legacy | enviado canónico |
|---:|---:|---:|---:|---:|
| 0 | 39 | 44 | 70.2 | 57.5 |
| 1 | 41 | 44 | 126.9 | 199.3 |
| 2 | 41 | 44 | 378.8 | 378.8 |
| 3 | 45 | 43 | 55.0 | 131.7 |
| 7 | 43 | 41 | 152.7 | 37.9 |

En NORMAL ninguna seed canónica cae más de 4.7% frente a policy `0`. Targeting,
Watcher y no-conformidad quedan en cero en todas. PRODUCTION_TEST termina con
`37,43,45,39,42` vivos para seeds `0,1,2,3,7`.

CALM mantiene soporte 1 y cero eventos institucionales de presión, pero termina
con `0,36,39,43,43` vivos. La extinción de seed 0 es la deuda conocida que antes
ocultaba la sanción semántica por comer: sin una cadena inicial, esa topología no
funda producción alimentaria robusta. No se reintroduce vigilancia para rescatar
la seed; la Fase 4 ataca directamente la causa mediante infraestructura heredada.

## Fase 4 - Fábrica heredada, no fundada

### Meta

Hacer que el mundo inicial exprese la premisa narrativa antes de que los agentes
tomen su primera decisión.

### Trabajo

1. Generar al menos una cadena mínima preexistente: FoodMachine, MaterialsMachine,
   OutputMachine, almacenamiento, Exit y una ruta logística parcial o completa.
2. Inicializar la cadena usada, degradada y subóptima, no como una solución ideal.
3. Garantizar reachability y capacidad mínima con tests de propiedades del mapa,
   no con coordenadas fijas.
4. Mantener oportunidades para reparación, mantenimiento, desvío y ampliación.
5. Replantear BUILD: primero conserva o adapta infraestructura heredada; construir
   desde cero deja de ser requisito para que exista comida procesada u output.
6. Separar estructuras institucionales de espacios apropiados por habitantes.
   Una cantina oficial puede existir, pero no debe ser la única fuente posible de
   congregación ni presentarse como espacio emergente.
7. Restaurar Entrance solo si representa suministro o llegadas externas reales;
   de lo contrario, eliminarlo del modelo y de la documentación.

### Puerta de salida

- `vida_batch map <seed>` muestra una cadena reconocible y alcanzable en las 20
  semillas de validación.
- El primer tick ya contiene producción potencial sin ningún BUILD previo.
- Desactivar BUILD no elimina la existencia de la fábrica, aunque impida su
  expansión y reparación a largo plazo.

### Resultado verificado (2026-07-21)

WFC coloca antes de la población una FoodMachine y una MaterialsMachine sobre
recursos renovables, una OutputMachine con input inicial, tres Storages, cuatro
conveyors dirigidos al Exit y condiciones `0.55,0.65,0.75,0.85`. La conversión a
`Grid` conserva recursos, buffers, condición y estado construido. La
infraestructura inicial se contabiliza separada de los eventos de construcción
de habitantes.

Los tests de propiedades verifican en veinte seeds que la cadena mínima existe,
es determinista, alcanzable y conecta la OutputMachine con el Exit. Un fixture de
ocho ticks opera FOOD, construction material, OUTPUT y shipping sin seleccionar ni
ejecutar BUILD. `allow_build = false` excluye BUILD de selección, targeting y
ejecución sin retirar la infraestructura heredada. `Entrance` fue eliminado del
modelo ejecutable porque todavía no representa un flujo externo real.

La revisión de cierre corrigió mecanismos previos expuestos por la cadena inicial:
utilidades cero ya no reciben peso softmax; BUILD puede colocar, completar y
recuperar una OutputMachine con construction material; la consulta de planificación
de conveyors dejó de mutar el mapa; cada belt conserva un solo tipo de recurso; el
hauling deposita únicamente al llegar al Storage; `ProductionChain::assess()` cuenta
buffers de máquinas y conveyors; y el recurso bajo una máquina averiada continúa
regenerándose sin auto-gather hasta su reparación.

CTest termina `3/3`. La regresión a 3000 ticks en seeds `0,1,2,3,7` termina con:

- NORMAL: `47,48,48,48,48` vivos;
- CALM: `48,48,48,48,48` vivos;
- PRODUCTION_TEST: `48,44,48,48,48` vivos.

No aparece ninguna caída superior al 30% respecto del baseline previo. La
exclusión general de acciones sin efecto, IDLE como candidato explícito y la
localización de señales globales siguen perteneciendo a la Fase 5.

## Fase 5 - Decisión individual local y legible

### Meta

Conservar utilidad estocástica, pero hacer que las decisiones procedan de
necesidades, personalidad, habilidad y observaciones que el agente puede poseer.

### Trabajo

1. Representar explícitamente por acción `U_self`, `U_factory`, costo/riesgo y
   utilidad final. Exponer la descomposición en métricas de debug.
2. Excluir del softmax acciones sin target o efecto factible. Incluir IDLE como
   candidato real de bajo valor; utilidad cero deja de ser una lotería de acciones
   imposibles.
3. Eliminar routing por `agent.id % N` y asignación poblacional implícita.
4. Sustituir señales globales de `ProductionChain::assess()` por observación local,
   memoria de lugares visitados o señales institucionales visibles en máquinas y
   storages.
5. Mantener pathfinding global como servicio técnico solo si no entrega al agente
   conocimiento sobre recursos que nunca observó.
6. Hacer que habilidad aumente eficacia esperada y, suavemente, utilidad; nunca
   imponer una profesión mediante un gate.
7. Añadir progreso para habilidades artística y social, y definir si existe
   olvido por desuso. El documento y el código deben escoger la misma regla.
8. Recalibrar estrés de necesidades superiores: o tiene un efecto medible o se
   elimina la afirmación. Evitar inputs nominales siempre anulados por decay.
9. Retirar efectos conductuales residuales de `StressState::REDEEMED`: sin
   inmunidad, reescritura instantánea de personalidad ni destino de mártir.
10. Aplicar cada cambio como A/B independiente sobre `0 1 2 3 7`.

### Puerta de salida

- No existe routing productivo por ID ni cuota fija de trabajadores.
- Ninguna acción seleccionada carece de target factible en ese tick.
- La utilidad puede explicarse desde estado individual y observaciones registradas.
- La especialización aumenta gradualmente con práctica sin asignación central.
- Se preserva la regresión de supervivencia y se mide el impacto de cada cambio.

### Resultado verificado (2026-07-22)

`ActionComponent` conserva para las trece acciones una descomposición uniforme
`UtilityBreakdown { self, factory, cost, risk, final, feasible }`. El JSON agrega
muestras y medias por acción. Costo y riesgo permanecen explícitamente en cero
donde el modelo no contiene todavía un término esperado, en vez de quedar
implícitos en fórmulas narrativas.

El softmax recibe únicamente acciones factibles y ahora incluye IDLE con utilidad
baja `0.02`. Utilidad y targeting comparten predicados puros para WORK y contratos
equivalentes para el resto. Las carreras entre planes válidos se registran como
`plan_invalidations`, separadas de errores de targeting; las quince corridas de
regresión terminan con cero `target_failures`.

Las señales canónicas de comida, materia, infraestructura, personas y lugares se
calculan en su mayoría dentro de un radio observable de doce tiles.
`sim_utility.cpp` y `sim_targets.cpp` ya no leen `ProductionChain::assess()` para
decidir. Persisten dos excepciones técnicas explícitas: el planner de conveyors
escanea conectividad fabril global antes de filtrar el sitio visible y A* conserva
el mapa completo después de elegir un target visible o recordado. El test
contrafactual modifica cien unidades de comida fuera del radio y verifica igualdad
exacta de utilidad, acción y target.

Se eliminó `agent.id % 10` y la secuencia fija de arquetipos por índice. Los
targets WORK se puntúan continuamente por input cargado/visible, distancia, claim,
hambre y habilidad; los arquetipos iniciales se muestrean y solo generan rasgos
continuos. La readiness industrial quedó acotada para no superar artificialmente
la curva de supervivencia.

Factory, domestic, artistic y social progresan solo cuando la acción tuvo efecto,
a razón de `0.1 XP` por tick efectivo. La habilidad aumenta eficacia y utilidad de
forma suave, sin gates; no existe olvido por desuso. Las métricas exponen los cuatro
promedios. Los inputs nominales de estrés por necesidades superiores se retiraron:
estas necesidades actúan mediante utilidad y mood, no mediante una contribución
menor que el decay. Redemption queda como evento factual de Chronicle; se retiraron
`StressState::REDEEMED`, inmunidades, healing, exenciones y reescritura de rasgos.

CTest termina `3/3`. La regresión a 3000 ticks en seeds `0,1,2,3,7` termina con:

- NORMAL: `40,45,48,46,41` vivos;
- CALM: `47,45,48,43,42` vivos;
- PRODUCTION_TEST: `36,45,48,42,42` vivos.

La mayor caída frente a Fase 4 es 25% en PRODUCTION_TEST seed 0, menor que la
alarma establecida de 30%. IDLE aparece como elección real y acotada; la siguiente
fase debe medir si los patrones sociales/espaciales sobreviven sin privilegios de
facción, EatingZone o artefactos por tick.

## Fase 6 - Cultura, espacios y organización como resultados

### Meta

Eliminar privilegios discretos de las etiquetas colectivas y medir patrones
espaciales/sociales que persistan sin atractores obligatorios.

### Trabajo social

1. Actualizar confianza y familiaridad por copresencia, colaboración, conflicto y
   creación compartida, no solo por ejecutar SOCIALIZE.
2. Derivar comunidades desde el grafo para observación. Una “facción” no añade
   comida, significado, protección o utilidad por sí misma.
3. Expresar ayuda, reparto y coordinación como funciones continuas de confianza,
   necesidad, proximidad e influencia.
4. Corregir la dirección de interacciones negativas: quien presencia una acción
   decide su confianza hacia quien la ejecuta.
5. Separar opinión política, preferencia estética y confianza interpersonal si se
   pretende afirmar que existen subculturas artísticas.

### Trabajo espacial y cultural

1. Añadir memoria o afinidad por ubicación basada en experiencias repetidas.
2. Puntuar lugares por propiedades observables como distancia al ruido, tráfico,
   seguridad, alimento, personas conocidas y artefactos, sin asignar una zona por
   arquetipo.
3. Permitir SOCIALIZE, REST y CREATE fuera de EatingZone cuando otro lugar ofrece
   mayor utilidad local.
4. Crear un artefacto al completar una unidad de trabajo creativo, no cada tick de
   una acción pegajosa.
5. Si CREATE requiere insumos, usar un recurso físico trazable; no un costo
   simbólico oculto.
6. Mantener Chronicle factual. “Resistencia”, “belleza”, “martirio” y “unidad” son
   interpretaciones opcionales de la vista narrativa y nunca entradas del modelo.

### Métricas de emergencia

- persistencia temporal de clusters espaciales;
- distancia entre agentes similares comparada con personalidades barajadas;
- modularidad y estabilidad de comunidades sociales;
- entropía de acciones por agente y especialización población/agente;
- distribución contribución-beneficio para estudiar free-riding;
- comparación con social learning, afinidad espacial o artefactos desactivados.

### Puerta de salida

No se declara “subcultura”, “segregación”, “liderazgo” o “free-riding” sin una
diferencia multisemilla respecto de su contrafactual. El resultado puede ser que
un fenómeno no emerja; eso es evidencia válida, no un motivo para añadir un gate.

### Resultado verificado (2026-07-22)

Las etiquetas dejaron de ser estado conductual. `community_id` se deriva cada
cincuenta ticks únicamente desde familiaridad y confianza recíprocas, se usa para
observación y métricas y está prohibido por auditoría estática en utilidad,
targeting, ejecución y política institucional. Se retiraron el reparto preferente,
significado, modulación de confianza, arrastre de opinión, protección, exención de
reportes y targeting fabril que dependían de facciones. Incluso la política legacy
puntúa ahora solo estado físico.

La evidencia social distingue roles y dirección. Copresencia aumenta lentamente
familiaridad sin inventar confianza; BUILD, WORK o CREATE efectivos y compartidos
aumentan relación recíproca; recibir ayuda aumenta confianza hacia quien ayudó;
observar conflicto cambia solo `observer -> actor`. Se corrigieron las llamadas de
sabotaje, el duelo, el decay y el aprendizaje de opinión direccional. El reparto de
comida depende continuamente de excedente, hambre y capacidad del receptor,
distancia, confianza, gregariousness e influencia, no de membresía.

Cada agente conserva hasta 24 experiencias espaciales con afinidad, exposiciones y
último tick. REST, SOCIALIZE y CREATE comparten planes puntuados por afinidad,
distancia, tráfico observado, ruido de maquinaria, riesgo físico, acceso a comida,
personas conocidas y artefactos visibles. Los targets recordados pueden usar la
afinidad aprendida, pero no leer el estado actual fuera del radio observable de
doce tiles. EatingZone ya no concede capacidad, satisfacción o utilidad y CREATE
ya no requiere OpenSpace.

CREATE permanece sin insumos y acumula trabajo físico explícito: por defecto una
unidad requiere veinte ticks efectivos, conserva residuo y produce exactamente un
artefacto y un evento al completarse. Los efectos de artefactos dependen de la
artistry individual y pueden desactivarse sin retirar CREATE. No se añadió una
preferencia estética porque no se formula una hipótesis de subcultura artística;
artistry continúa siendo habilidad/rasgo, no gusto compartido.

Chronicle almacena hechos como `POST_SABOTAGE_PAUSE`, comunidad detectada y unidad
de artefacto completada. “Redención”, “belleza”, “resistencia” y otros juicios
quedan solo en la presentación narrativa, que continúa siendo neutral respecto del
RNG y de la conducta.

El JSON schema 2 expone:

- Jaccard temporal de pares espaciales dentro de radio 3, muestreado cada 50 ticks;
- distancia del vecino más similar frente a una permutación determinista de rasgos;
- modularidad del grafo y estabilidad de pares cocomunitarios;
- entropía de las trece acciones por agente y especialización población/agente;
- ledger por agente de ticks productivos y comida compartida, recibida y consumida;
- toggles independientes para social learning, afinidad y efectos de artefactos.

Un contraste CALM emparejado de 20 seeds a 1000 ticks produjo estas medias; las
cuatro variantes conservaron 48/48 agentes en todas las seeds:

| Variante | Persistencia | Delta distancia vs shuffle | Modularidad | Estabilidad | Entropía | Especialización poblacional | Corr. contribución-beneficio | Artefactos |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Completa | 0.238 | 1.039 | 0.443 | 0.672 | 0.688 | 0.118 | 0.693 | 242.6 |
| Sin social learning | 0.226 | 0.449 | 0.000 | 0.000 | 0.690 | 0.115 | 0.686 | 241.9 |
| Sin afinidad | 0.198 | 0.236 | 0.429 | 0.670 | 0.687 | 0.115 | 0.633 | 229.3 |
| Sin efectos de artefactos | 0.232 | 0.788 | 0.426 | 0.666 | 0.692 | 0.117 | 0.717 | 253.9 |

El aprendizaje social explica la aparición de componentes comunitarios y la
afinidad incrementa la persistencia espacial en este horizonte. No se declara
segregación: el delta frente a shuffle carece todavía de intervalo estadístico y
de persistencia larga. Tampoco se declara subcultura, liderazgo o free-riding; no
hay preferencia estética modelada, no se midió precedencia causal de influencia y
la contribución-beneficio observada es positiva. Estos resultados nulos se
conservan como evidencia en lugar de introducir gates.

La regresión a 3000 ticks en seeds `0,1,2,3,7` termina con cero target failures:

- NORMAL: `44,39,48,47,46` vivos;
- CALM: `46,46,48,45,45` vivos;
- PRODUCTION_TEST: `43,42,48,42,43` vivos.

La mayor caída frente a Fase 5 es 13.3% en NORMAL seed 1, menor que la alarma de
30%. CTest termina `3/3`; una corrida NORMAL seed 0 de 3000 ticks tarda
aproximadamente 18.6 segundos en este host.

## Fase 7 - Ciclo vital y generaciones

### Meta

Permitir historia social de largo plazo sin reponer automáticamente la población
hasta un número objetivo.

### Trabajo

1. Introducir edad, esperanza de vida y mortalidad natural separada de crisis.
2. Añadir llegadas por Entrance mediante una regla externa neutral y configurable,
   no como reemplazo inmediato de cada muerte.
3. Inicializar recién llegados sin relaciones y permitir integración mediante
   encuentros reales.
4. Muestrear personalidad desde distribuciones documentadas; no forzar una
   secuencia balanceada de seis arquetipos.
5. Añadir reproducción solo después de estabilizar llegadas: condiciones
   materiales/sociales continuas, herencia con variación y ningún rol predestinado.
6. Transmitir cultura por relaciones, convivencia, opinión y objetos persistentes,
   no copiando etiquetas de facción o arquetipo.
7. Ejecutar pruebas de 10 000+ ticks y registrar genealogía, movilidad social y
   supervivencia por cohorte.

### Puerta de salida

Existen al menos dos cohortes observables, la población puede crecer o extinguirse
sin corrección hacia `initial_population`, y los descendientes no reciben una
profesión o facción codificada.

### Resultado verificado (2026-07-22)

La identidad personal es histórica y monotónica: ningún ID se reutiliza.
`SocialFabric`, Chronicle y los ledgers por agente crecen cuando el historial
supera su capacidad inicial; Chronicle deja de truncar biografías por encima de
64. Los muertos conservan su entidad para análisis, pero liberan claims físicos.
Cada persona posee además un stream RNG propio, por lo que añadir un residente no
observado no desplaza la secuencia de decisiones de los existentes.

WFC coloca una Entrance alcanzable en la pared izquierda y conserva Exit en la
derecha. Entrance se deriva de la Y ya muestreada para Exit y no consume otro draw,
por lo que no altera el resto del layout aleatorio. Las veinte seeds de mapa
contienen exactamente una Entrance, una Exit y la cadena heredada alcanzable.

`LifecycleComponent` registra origen, padres, entrada, edad inicial, lifespan,
cohorte, generación, cooldown reproductivo, muerte, primera integración y pico de
influencia. La edad se deriva de ticks; el lifespan es una función hash de seed e
ID alrededor de 8000 ticks con dispersión 20%. Mortalidad natural entra en el
mismo pipeline exclusivo después de hambre, agotamiento y breakdown, con evento y
causa métrica propios.

Las llegadas siguen un proceso exógeno de tiempo y seed, por defecto 0.8 intentos
esperados por 1000 ticks. No leen muertes, población inicial, fábrica o estado
social. `max_population` limita personas vivas; un intento bloqueado se descarta y
no forma backlog. Un contrafactual con y sin muerte registra los mismos intentos y
admisiones cuando existe capacidad, conservando una diferencia de una persona en
vez de reponerla.

Reproducción se evalúa cada 50 ticks entre pares maduros y próximos. Su probabilidad
continua combina seguridad alimentaria local, hambre, descanso, stress, mood,
familiaridad y confianza recíprocas, edad y cooldown. Los traits del descendiente
son el promedio parental más mutación acotada `+-0.08`; skills, XP, comunidad,
arquetipo, relaciones, opiniones culturales, memoria espacial y progreso creativo
no se copian. Los recién llegados y nacidos se publican al final del tick, por lo
que su integración comienza únicamente con encuentros posteriores.

El JSON schema 3 añade contabilidad histórica, capacidad y pico poblacional,
intentos/admisiones/bloqueos, nacimientos, mortalidad natural y `demography` con:

- genealogía y generación por ID estable;
- cohortes temporales con person-ticks, censura viva y muertes por causa;
- latencia hasta la primera arista recíproca de confianza;
- pico y valor terminal de influencia como movilidad social continua;
- origen, edad, lifespan, padres y traits de cada persona.

Con mortalidad, llegadas y reproducción apagadas, la regresión a 3000 ticks frente
a Fase 6 conserva cero target failures y su peor caída es 12.5% en PRODUCTION_TEST
seed 2, menor que la alarma de 30%. Con demografía canónica activada, las seeds
`0,1,2,3,7` terminan con:

- NORMAL total: `51,48,51,48,50`; fundadores vivos: `48,42,46,43,44`;
- CALM total: `53,46,55,52,53`; fundadores vivos: `48,43,45,44,46`;
- PRODUCTION_TEST total: `49,41,43,46,44`; fundadores vivos: `48,40,42,44,41`.

La prueba CALM a 10 000 ticks produce:

| Seed | Pico | Históricas | Llegadas | Nacimientos | Natural | Hambre | Vivas | Cohortes | Gen. máxima |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 57 | 63 | 5 | 10 | 51 | 0 | 12 | 9 | 2 |
| 1 | 51 | 64 | 9 | 7 | 47 | 5 | 12 | 8 | 3 |
| 2 | 64 | 71 | 7 | 16 | 55 | 4 | 12 | 9 | 2 |
| 3 | 56 | 71 | 13 | 10 | 48 | 7 | 16 | 10 | 2 |
| 7 | 57 | 67 | 10 | 9 | 51 | 2 | 14 | 8 | 2 |

En las cinco seeds no queda ningún fundador vivo: la población restante combina
llegados y descendientes, y registra cero target failures. El pico supera la
población inicial y luego puede caer muy por debajo sin corrección; un fixture
separado demuestra extinción persistente con entradas y reproducción apagadas.
Otro fixture ejecuta 10 000 ticks dos veces y exige replay idéntico, IDs únicos,
capacidad viva acotada y cierre `históricas = vivas + muertes`.

La puerta original de Fase 7 terminó `3/3` incluyendo ambos runs largos. CALM seed
0 a 10 000 ticks tarda aproximadamente 81 segundos en este host. La GUI conserva
selección por ID estable y muestra población viva/histórica; Fase 8 completó después
su build portable y la separación entre vista de jugador y debug.

## Fase 8 - Director humano indirecto

### Meta

Implementar la promesa jugable de intervenir sobre la institución sin emitir
órdenes a individuos.

### Trabajo

1. Añadir controles de cuota, zoning, construcción/eliminación institucional y
   prioridad de mantenimiento en la GUI.
2. Prohibir seleccionar una acción, target o utilidad para un agente concreto.
3. Registrar cada intervención con tick y parámetros.
4. Añadir replay de intervenciones al batch para reproducir partidas y probarlas.
5. Separar vista de jugador y vista de debug. Utilidades internas, personalidad
   exacta y estado global pueden existir en debug, pero no asumirse como
   información del Director.
6. Mostrar consecuencias observables: flujo, stocks, deterioro, concentración,
   eventos y relatos factuales.

### Puerta de salida

Un replay produce el mismo resultado que la sesión original para una semilla, y
ninguna API de Director acepta un `agent_id` como sujeto de una orden conductual.

### Estado implementado

El límite del Director vive en `director.h` y `sim_director.cpp`. Sus comandos
tipados solo aceptan cuota, coordenadas ambientales, capacidad anónima de ocupación,
tipo de estructura, dirección de conveyor o prioridad física de mantenimiento. No
aceptan identidad, acción, target conductual, personalidad, relación ni utilidad.

El zoning implementado es deliberadamente físico: capacidad de ocupación `0..8`
sobre Floor/OpenSpace, sin zonas de profesión, cultura o asignación personal. La
construcción institucional coloca Wall, Storage, las tres máquinas o Conveyor ya
terminados; las máquinas sobre fuentes preservan y restauran el depósito al ser
eliminadas. La eliminación contabiliza stocks descartados como pérdida material.
La prioridad de mantenimiento solo pondera conveyors degradados visibles; no repara,
selecciona habitantes ni convierte una acción no factible en una orden.

Cada intervención aceptada recibe el `tick` previo a `Simulation::advance()` y una
secuencia global estricta. `vida_gui --seed N --record archivo.toml` escribe un log
TOML schema 2 con huella FNV-1a de la fuente de configuracion; `vida_batch replay
<ticks> <seed> <archivo.toml>` valida cabecera, seed, huella, orden, ticks y
transiciones antes de reproducirlas. Su salida JSON separada conserva schema 1. Un
fixture compara sesión original y replay sobre métricas, grid, población, cuota,
Chronicle y ledger; el CTest de CLI exige además salida byte-idéntica y rechazo de
seed o configuracion distinta.

La GUI ejecuta simulación y render en el mismo thread, eliminando la carrera de
datos anterior. `E` abre los cinco controles institucionales y pausa la simulación;
la vista normal muestra demanda/fill, stocks, flujo enviado, deterioro, densidad,
zonas, prioridades y eventos factuales. `F12` abre explícitamente la vista debug con
necesidades, personalidad, relaciones y utilidades exactas. CMake consume targets
SDL2/SDL2_ttf de paquetes estándar o vcpkg y conserva fallback Unix/Homebrew; el
build MSVC copia las DLL y la búsqueda de fuentes admite `%WINDIR%` y
`VIDA_FONT_PATH`.

La regresión NORMAL a 3000 ticks para seeds `0,1,2,3,7` termina con
`51,48,51,49,50` habitantes vivos frente a `51,48,51,48,50` al cerrar Fase 7:
no hay caída y todos los target failures permanecen en cero. CTest termina `4/4`,
incluyendo contratos de intervención, round-trip/replay, CLI y auditoría estática
del límite conductual. La GUI compila y arranca en Windows con MSVC y vcpkg.

## Fase 9 - Consolidación documental

> **Estado (2026-07-22): Completada.** README, comentarios canónicos/legacy,
> guías operativas, fuentes académicas y planes históricos fueron reconciliados;
> los artefactos se regeneraron y verificaron contra el ejecutable.

### Meta

Convertir la documentación en una fuente de verdad con niveles explícitos de
madurez.

### Trabajo

1. Reescribir `doc/design_spec.md` con etiquetas **Implementado**, **Objetivo** y
   **Hipótesis por validar** por sección.
2. Actualizar README y `config/default.toml` desde el comportamiento ejecutable,
   eliminando modelos de dos máquinas, hot reload y cifras antiguas.
3. Sincronizar las secciones académicas 03, 07, 12, 13, 14, 15, 16, 17, 18, 19 y
   20 con los mecanismos realmente activos.
4. Marcar `2026-05-30-factory-as-antagonist.md` como histórico y superado en su
   interpretación estratégica, sin borrar el registro de decisiones.
5. Documentar resultados negativos: si no aparece segregación, free-riding o
   generaciones culturales, no presentarlos en tiempo presente.
6. Regenerar y revisar los HTML/PDF mediante
   `bash doc/bases_matematicas/build.sh`.
7. Actualizar `AGENTS.md` si cambian comandos, targets o protocolo de regresión.

### Puerta de salida

Cada afirmación en presente tiene una ruta de código y, cuando describe un
resultado emergente, una métrica o experimento reproducible asociado.

### Resultado verificado (2026-07-22)

`doc/design_spec.md`, README, ROADMAP, configuracion, guias y secciones academicas
describen el modelo canonico y separan hechos, objetivos e hipotesis. El plan
adversarial queda marcado como historico. `doc/bases_matematicas/build.sh` regenero
HTML y PDF con referencias revisadas; ambos artefactos reflejan schema 2 para logs
de intervencion, schema 1 para la salida JSON de replay y schema 3 para metricas.
La auditoria final no atribuye como resultados demostrados segregacion, subcultura,
liderazgo, free-riding ni transmision cultural multigeneracional.

La revision tecnica final corrigio factibilidad de EXPLORE sin destino, coherencia
local al ampliar OutputMachine, invalidacion de rutas tras edicion, el contador de
closures y replay schema 2 con huella de configuracion. CTest termina `4/4`; la
regresion NORMAL a 3000 ticks para seeds `0,1,2,3,7` termina con
`51,46,50,53,54` habitantes vivos y cero target failures. Ninguna seed cruza la
alarma de caida de 30% frente al cierre de Fase 8.

## Protocolo de implementación

1. Capturar baseline antes de cada fase.
2. Cambiar un mecanismo causal por vez.
3. Compilar `vida_batch` y ejecutar smoke de 200 ticks.
4. Ejecutar `analysis` y `metrics` a 3000 ticks para `0 1 2 3 7`.
5. Comparar distribución, no solo promedio. Una caída de vivos superior a 30% en
   cualquier semilla requiere explicación antes de continuar.
6. Usar 20+ semillas para aceptar una afirmación macro.
7. Guardar variantes solo mientras tengan una pregunta A/B concreta. No acumular
   caminos legacy sin uso experimental.
8. Actualizar la sección documental afectada en la misma fase; la Fase 9 realiza
   la consolidación, no posterga toda la verdad hasta el final.

## Definición de terminado

El rediseño completo se considera terminado cuando:

- bloquear output enviado produce contracción material y mortalidad indirecta;
- restaurar output permite recuperación con retardo;
- la fábrica no inspecciona ni selecciona estados sociales o identidades;
- el mundo comienza con una fábrica productiva heredada;
- las decisiones no se reparten por ID ni omnisciencia global implícita;
- las acciones no factibles no entran al softmax;
- cualquier facción, liderazgo o subcultura reclamada se deriva mediante métricas,
  no mediante buffs discretos; actualmente solo `community_id` se deriva;
- CALM elimina toda presión institucional;
- llegadas y generaciones crean historia de largo plazo sin reposición forzada;
- el Director modifica el entorno pero no controla habitantes;
- Chronicle y métricas describen hechos completos y consistentes;
- documentación, configuración y ejecutable describen el mismo modelo.

## Riesgos principales

| Riesgo | Mitigación |
|---|---|
| La dependencia de output causa extinciones abruptas | EMA, retardo, suelo de suministro y prueba de recuperación |
| Eliminar routing global rompe la cadena | Localizar señales gradualmente y medir target failures |
| Quitar buffs de facción elimina toda organización | Mantener efectos continuos de confianza antes de retirar etiquetas |
| La fábrica preconstruida vuelve trivial el juego | Estado inicial degradado, capacidad insuficiente y mantenimiento costoso |
| Las generaciones ocultan fallos de supervivencia | Separar cohortes y no reponer hacia una población objetivo |
| Las métricas redefinen el fenómeno para “hacerlo aparecer” | Fijar métrica y contrafactual antes del experimento |
| El plan crece demasiado | No iniciar una fase hasta cerrar la puerta de salida anterior |

## No objetivos

- No convertir la fábrica en una IA que optimiza contra los habitantes.
- No usar un LLM para decisiones, causalidad o clasificación de emergencia.
- No garantizar que toda semilla sobreviva o produzca cultura.
- No eliminar toda regla discreta; sí exigir que represente una propiedad real y
  supere un A/B, no que parchee un síntoma.
- No preservar por compatibilidad todo comportamiento histórico. Las variantes
  legacy necesitan una función experimental concreta.
