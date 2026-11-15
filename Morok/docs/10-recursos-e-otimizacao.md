# 10. Recursos e otimização

## 10.1 DEC-011 — Limites de memória

Hardware de referência inicial: 4 GB de RAM física.

Metas provisórias:
- alvo de consumo do processo: até 512 MB;
- teto operacional inicial: 1 GB;
- treinamento com lotes pequenos;
- processamento por fluxo;
- evitar cópias de buffers;
- controlar alocações e tamanho de estados.

O teto de 1 GB não é objetivo de consumo constante. O sistema operacional e outros processos precisam manter memória disponível.

## 10.2 Precisão

FP32 será usado inicialmente para depuração e validação. FP16 e INT8 serão avaliados em etapas posteriores, se houver suporte adequado e ganho real sem degradação inaceitável.

## 10.3 Otimizações prioritárias

1. Evitar cópias desnecessárias.
2. Reutilizar buffers.
3. Processar em lotes pequenos.
4. Limitar candidatos de segmentação.
5. Reavaliar agrupamentos periodicamente, não a cada byte.
6. Limitar o tamanho do estado recorrente.
7. Evitar estruturas dinâmicas com overhead alto.
8. Medir antes de introduzir paralelismo.
9. Separar memória de treinamento e inferência.
10. Usar checkpoints compactos e verificáveis.

## 10.4 Otimizações não presumidas

Não será presumido que:
- uma saída binária é sempre mais rápida;
- recorrência é sempre melhor que atenção;
- compressão reduz sempre o custo total;
- mais hierarquia aumenta qualidade;
- quantização é sempre vantajosa;
- mais parâmetros melhoram automaticamente o resultado.

Cada hipótese exige experimento controlado.

## 10.5 Orçamento de recursos

O relatório de cada experimento deverá registrar RAM máxima, tempo, throughput, latência, tamanho dos dados e configuração de hardware. Se a meta de memória for ultrapassada, o experimento deverá reduzir lote, contexto ou número de candidatos antes de ampliar recursos.
