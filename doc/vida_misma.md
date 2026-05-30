2026-05-28
La vida misma es como una fábrica, es una fábrica que no tiene ningún sentido, que produce algo que no se entiende, algo que un agente externo controla.

Dentro de la vida misma, pequeños agentes que no entienden por qué llegaron ahí asumen roles, la fábrica tiene que funcionar, su funcion de utilidad depende de eso.

Pero también su función de utilidad tiene otras cosas, ellos quieren hacer otras cosas, quieren vivir, tienen impulsos internos. 

Entonces generan espacios dentro de la fábrica, espacios organizados para poder sobrellevar el tiempo.

Algunos son más complacientes, otros son más obedecedores. Todos saben que si la máquina no funciona morirán.

Pero todos quieren trabajar lo menos posible, esto significa que algunos trabajan más y otros menos.

Hay gente que nace artista, hay gente que nace músicos. Todos viven en esta fábrica

La fábrica tiene que tener reglas, tienen que haber recursos que tienen que ir y salir hacia otra parte.


En la fábrica vive gente, en la fábrica muere gente, la fábrica genera trabajo


Hay entradas en las tres dimensiones de la fábrica, cosas que vienen desde arriba, afuera, cosas que vienen desde abajo, cosas que vienen desde los lados?

O la fábrica deberia ser un 2d, quizás una grilla2d es suficiente al principio, para qué complejizarj

# 2026-05-30 13:22: On the resources Types

Los recursos deben generarse proceduralmente, al principio del RUN, la fábrica parte con 2 Storages Units y el Common Room que los rodea. El ScrapPile debe ser suficiente para Construir dos máquinas

1.- La foodMachine para consumir la FoodSource (a un rate ineficiente menor a la regeneracion, la idea es que en el futuro implementaremos mejoras de eficiencia) la ineficiencia pone presión al ambiente (la fuente de comida depletea en T-> ticks altos)

2.- la materialsMachine -> te permite construir blocks para  construir la outputMachine, el argumento es que los scrapPiles te sirven para generar todas las otras machines (raw_material) pero es un recurso finito, asi que necesitas construir la materialsMachine para poder trabajar, el materialsMachine al producir genera scrap entonces con eso puedes reciclar, pero debe ser usado con la scrapPile

3.- outputMachine -> Survival of the factory, regenerates health of the factory, la health of the factory en verdad es el designio del agente maestro.

## Sobre el documento

Hay que actualizar las secciones 12.2 y 12.3 para representar las nuevas actualizaciones y definiciones

12.4 Should also be updated 

- FoodSource can be gathered raw for (`), but can cause disease if eated raw
- FoodSource needs to be procesed in machine


## Fallas generales:

Factory goes to 0 health but agents are still alive? how if the factory is crumbling, Factory should enforce output production
