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


# 2026-05-30 18:11 On The Agents stress

The stress should have consequences on agents, as they do to people in the real life:
    - Permanent disability to perform well -> your brain has permanently change now your top experience is lower
    - Heavy dissosiation with life -> paranoia, illness, unwilingness to make anything
    - High euphoria and desapego with your context
    - Extreme individualism, goal oriented stress -> permanent disability or derrame cerebral
    The most interesting thing to develop with people that are permanently stressed is the inability to connect with other, causing the other agents to exclude them and appart them so they dont become contagieous, also psycopathy or stuff like thats, so the stressed out is not only an internal problem of the agents, also its a external inductor of problems

The model follows the logic of contemporary society where our economic system is willing to erase all traces of human expression using us just as cogs in the machine of valor. So its like this. Stress is not reduced by mere resting, it needs to be extirpated, doing the things that you, as an agent with creativity want to really do. But if certain trheshold is surpassed you get to a point of no return.

    - The stressed people are willing to interfere with the machine, sabotage -> if im stressed then everyone will be, this could be xpressed with a permanent change to the utility function (stressed version of utility funciton)
    
    A good simulation will always be a simulation capable of replicating the simple dynamics of real life. 

## About the simualtion itself

It seems that the factory is not big enough yet, i suggest a 100x100 tile so it can have more things, also its important to note that the factory map and the properties (initial stage) should be generated in the Wave Function manner, when i run ./build/vida_misma STORAGE and MACHINES are always in the same places.
Factory seems to have a belt going from storage to output by default, It shouldnt, the workers should build it.

## About the doc/
design_spec should be updated more often because its our core design document, plans should be in plans, this document is the human interface (i write on this as a human). some documents in root (like PHASE8_FIXPLAN.md) if are no longer needed should be archived or removed