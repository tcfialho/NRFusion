# Fusão do candidato 1, medida

A cadeia é a que os blocos 23..47 realmente têm: projeção 512x512, depois rede densa por
grupo de 64 canais, 64 -> 256 -> 64, com ativação no meio. Quinze blocos a executam.

    nvcc -O3 -arch=sm_89 grouped_ffn.cu -o grouped_ffn

Medido numa RTX 4050 Laptop (sm_89), 50 execuções após 10 de aquecimento:

| versão | p50 | p95 |
|---|---|---|
| separada | 19.421 µs | 21.018 µs |
| fundida | 15.787 µs | 17.179 µs |

**Resultado histórico isolado: 18,7% na mediana, com diferença numérica zero entre as duas saídas do protótipo FP16.**

> EXPERIMENTO ISOLADO — NÃO VALIDA O PRODUTO FINAL.

As duas fazem a mesma aritmética, com os mesmos tiles e a mesma precisão. A única
diferença é onde os intermediários vivem: a versão separada escreve a projeção e a
expansão na memória global e as lê de volta; a fundida as mantém em memória
compartilhada, 40 KiB por CTA, dentro dos 48 KiB padrão.

## O que este número é e o que não é

As duas implementações do protótipo usam a mesma precisão. A igualdade observada entre
elas não comprova equivalência numérica com o runtime NVIDIA.

**Não é** uma previsão do ganho no jogo. Isto é uma reimplementação da cadeia, não os
kernels da NVIDIA. Se eles já fundem parte disso, a margem real é menor.

A versão separada usa um único lançamento com intermediários na memória global. Não
reproduz os lançamentos, as fusões existentes nem a sincronização do runtime original.
O resultado não estabelece um limite mínimo de ganho para o passe NR completo.
