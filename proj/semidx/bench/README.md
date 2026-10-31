# Benchmark de recuperación semántica

Esta batería evalúa los resultados de búsqueda de semidx con corpus y consultas
predefinidos. Es independiente de los tests funcionales de la biblioteca.

## Ejecución

Desde **proj/semidx/**, con las dependencias de kclib ya compiladas:

```sh
make linux/x86_64
python3 bench/run.py --binary ./bin/linux/x86_64/semidx
```

Para ejecutar solamente un conjunto o comparar las versiones española e inglesa:

```sh
python3 bench/run.py --binary ./bin/linux/x86_64/semidx cercanos-es
python3 bench/run.py --binary ./bin/linux/x86_64/semidx temas-es temas-en
```

También puede utilizarse una CLI instalada con `--binary semidx`. No se
descargan modelos ni se recompilan kclibs durante el benchmark. Python 3
solamente utiliza la biblioteca estándar.

## Conjuntos

- **temas-es**: separación entre temas generales en castellano
- **temas-en**: corpus y consultas equivalentes en inglés, como control
- **cercanos-es**: distinción entre conceptos técnicos relacionados
- **parafrasis-es**: búsquedas con vocabulario diferente al documento
- **rioplatense-es**: expresiones coloquiales y consultas cotidianas
- **polisemia-es**: una misma palabra con significados distintos

Cada archivo de **datasets/** contiene `ID<TAB>contenido`. Cada archivo de
**queries/** contiene `caso<TAB>ID esperado<TAB>consulta`. Los IDs son únicos
por dataset y las respuestas esperadas se fijan antes de ejecutar las búsquedas.

## Resultados

Para cada dataset, el script ejecuta `semidx build` y luego `semidx search`
con un límite de cinco resultados por consulta.

Los archivos se generan bajo **.build/bench/**:

- **summary.tsv**: documentos, consultas, Hit@1, Hit@5 y MRR@5 por dataset
- **NOMBRE/index.bin**: índice binario construido para ese dataset
- **NOMBRE/cases.tsv**: primer resultado, puntuación y posición del esperado
- **NOMBRE/results.tsv**: cinco resultados ordenados, con texto y similitud

Hit@1 mide la proporción de consultas cuyo primer resultado es el esperado.
Hit@5 indica si el esperado aparece entre los primeros cinco. MRR@5 mide su
posición, contando cero cuando no aparece entre esos cinco.

Las consultas tienen un solo documento relevante etiquetado como referencia.
Esto facilita una primera comparación, pero no implica que otros documentos
relacionados sean respuestas incorrectas en todos los usos. Los resultados no
demuestran por sí solos relevancia semántica general, y las similitudes absolutas
no deben compararse como probabilidades. La batería inicial tiene menos de
1.024 documentos por conjunto; mide principalmente embeddings y ranking
exhaustivo, no la recuperación aproximada de HNSW a gran escala.

Los aciertos o errores semánticos no determinan el estado de `make test`.
El script falla únicamente si faltan archivos, los datos son inválidos o fallan
los comandos de semidx.
