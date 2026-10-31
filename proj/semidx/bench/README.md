# Benchmark

Este benchmark compara `semantic` (HNSW), `lexical` (BM25),
`hybrid` (fusión RRF) y `rerank` (BM25 seguido de similitud
vectorial) con los mismos corpus y consultas etiquetadas. Es
independiente de los tests de contrato de semidx.

## Ejecución

Desde **proj/semidx/**, con el ejecutable de semidx ya compilado:

```sh
python3 bench/run.py --binary ./bin/linux/x86_64/semidx
```

Para ejecutar un conjunto específico o elegir modos:

```sh
python3 bench/run.py --binary ./bin/linux/x86_64/semidx rioplatense-es
python3 bench/run.py --binary ./bin/linux/x86_64/semidx temas-es temas-en
python3 bench/run.py --binary ./bin/linux/x86_64/semidx --mode lexical --mode hybrid
```

`--binary semidx` permite utilizar un ejecutable instalado en `PATH`.
`--output` cambia el directorio de resultados. No se recompila ninguna
biblioteca ni se descargan dependencias o modelos: se utiliza el ejecutable
existente y Python 3 con su biblioteca estándar.

## Conjuntos

- **temas-es**: distinción entre temas generales en castellano
- **temas-en**: documentos y consultas equivalentes en inglés
- **cercanos-es**: conceptos técnicos relacionados
- **parafrasis-es**: consultas con palabras distintas de las del documento
- **rioplatense-es**: expresiones coloquiales y consultas cotidianas
- **polisemia-es**: significados distintos de una misma palabra

Cada archivo de **datasets/** contiene `ID<TAB>contenido`. Cada archivo
de **queries/** contiene `caso<TAB>ID esperado<TAB>consulta`. Las etiquetas
son independientes de los resultados del sistema y se conservan iguales
para los cuatro modos.

## Resultados

Se crea un índice por conjunto y se ejecutan todas sus consultas en cada
modo seleccionado, con un límite de cinco resultados. Los archivos quedan
en **.build/bench/**:

- **summary.tsv**: métricas y tiempo medio por conjunto y modo
- **NOMBRE/index.bin**: índice compartido por los modos del conjunto
- **NOMBRE/cases.tsv**: posición del esperado, primer resultado y tiempo por consulta
- **NOMBRE/results.tsv**: resultados ordenados, con puntuación y contenido

**Hit@1** es la proporción de consultas donde el primer documento coincide
con el esperado. **Hit@5** comprueba si aparece entre los primeros cinco.
**MRR@5** promedia el inverso de su posición (cero si no aparece).

Las puntuaciones de BM25, coseno y RRF tienen escalas distintas; no deben
compararse directamente. La columna `mean_cli_ms` incluye el arranque
del proceso, la apertura del índice y la búsqueda. Como semidx reconstruye
HNSW al abrir un archivo, esta cifra mide la operación completa de la
CLI, **no** el tiempo aislado de búsqueda en un índice residente.
`build_ms` mide la construcción del índice una sola vez por conjunto.

Cada consulta etiqueta un único documento relevante; otro resultado
puede ser también válido semánticamente. Los conjuntos son pequeños y
permiten comparar rankings y lenguajes, pero no caracterizan la
escalabilidad de HNSW ni el rendimiento en corpus grandes. Los fallos de
relevancia se informan, sin convertirlos automáticamente en fallos de tests.
El ejecutor sale con error si faltan datos, hay resultados inconsistentes
o falla un comando.
