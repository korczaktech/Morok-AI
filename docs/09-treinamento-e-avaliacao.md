# 9. Treinamento, avaliação e reprodutibilidade

## 9.1 DEC-010 — Treinamento incremental

O treinamento será realizado do zero, em lotes pequenos e com processamento incremental compatível com a memória disponível.

Etapas conceituais:
1. aprender padrões básicos de bytes;
2. aprender agrupamentos intermediários;
3. aprender relações entre agrupamentos;
4. melhorar previsão contextual;
5. ajustar pesos adaptativos;
6. avaliar qualidade e recursos.

A ordem poderá ser alterada caso experimentos mostrem que treinamento conjunto é mais estável ou eficiente.

## 9.2 Separação dos dados

Os dados deverão ser separados em conjuntos de treinamento, validação e, quando possível, teste final. O conjunto de teste não deverá ser usado para ajustar hiperparâmetros.

Cada corpus deverá registrar:
- origem;
- licença e permissões;
- idioma e domínio;
- tamanho;
- procedimento de limpeza;
- hash ou identificador de versão;
- regras de deduplicação.

## 9.3 Reprodutibilidade

Registrar:
- semente aleatória;
- versão do código;
- versão da arquitetura;
- configuração completa;
- identificador dos dados;
- ambiente e compilador;
- precisão numérica;
- parâmetros iniciais;
- checkpoints;
- métricas por etapa.

## 9.4 Checkpoints

Checkpoints deverão armazenar parâmetros, versão da arquitetura, estado necessário para retomar o treinamento, contador de etapas e metadados. O formato deverá ser validado contra corrupção e incompatibilidade.

## 9.5 Métricas

Qualidade:
- perda de previsão;
- bits por byte, quando aplicável;
- acurácia de bits, como métrica auxiliar;
- validade UTF-8;
- qualidade de reconstrução;
- resultados em tarefas linguísticas pequenas e controladas.

Desempenho:
- tempo de treinamento;
- latência por byte e por sequência;
- throughput;
- operações estimadas;
- pico de RAM;
- tamanho do checkpoint;
- tempo de carregamento.

## 9.6 Baselines

A RLAH deverá ser comparada com referências simples:
- preditor de frequência de bytes;
- modelo recorrente pequeno sem segmentação adaptativa;
- modelo de bytes com segmentação fixa;
- variante sem hierarquia;
- variante sem compressão adaptativa.

As comparações deverão manter dados, divisão, orçamento e hardware tão equivalentes quanto possível.

## 9.7 Critério de conclusão

Uma etapa só será aprovada se os testes passarem, os resultados forem reproduzíveis, os limites de recursos forem respeitados e houver relatório de comparação. A execução isolada não comprova qualidade linguística.
