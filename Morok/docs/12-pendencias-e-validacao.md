# 12. Pendências e plano de validação

Este documento separa o que está especificado do que depende de implementação e evidência experimental. Os contratos operacionais da G1 foram fechados no documento 18.

## 12.1 Estado das especificações

Os seguintes pontos possuem padrões operacionais definidos:
- indexação causal da previsão do próximo byte;
- entrada fixa de 11 características e alvo binário de oito bits;
- política estrita de UTF-8;
- divisão de dados, deduplicação e prevenção de vazamento;
- hiperparâmetros iniciais, clipping, parada e early stopping;
- limites de RAM, leitura em fluxo e BPTT;
- limites de candidatos, frequência, cache e hierarquia;
- metas e controlador adaptativo;
- serialização com custo de metadados;
- geração, sessões, concorrência e checkpoints;
- testes de aceitação.

Os padrões são valores de partida. Sua qualidade não está validada e eles poderão ser alterados por experimento controlado, mantendo histórico de configuração.

## 12.2 Pendências de implementação

1. Organização final dos diretórios C++.
2. Sistema de build e compilador de referência.
3. Implementação das operações tensoriais e funções de ativação.
4. Gradientes manuais com justificativa de custo e verificações por diferenças finitas.
5. Inventário automático dos 1.000 parâmetros.
6. Formato de corpus e carregamento incremental.
7. Formato binário de checkpoint e testes de corrupção.
8. Testes unitários, integração e harness de benchmark: implementados; smoke test sintético executado em CI.
9. Baseline de frequência/softmax-256 unigram implementado; relatório agora inclui perplexidade por byte, delta/razão BPB, distribuição dos parâmetros e estatísticas de gradientes. Baseline softmax-256 neural condicionado ao estado para comparação direta com a fatoração binária ainda pendente.
10. Núcleo fast/slow implementado; pontuador e segmentador continuam pendentes de integração no caminho experimental.

## 12.3 Pendências de validação experimental

- Avaliar se d=11 oferece estabilidade e capacidade úteis em corpus representativo.
- Comparar BPTT 32/64/128 e learning rates definidos no documento 18.
- Comparar AdamW e SGD com momentum em múltiplas sementes.
- Comparar fatoração binária com baseline softmax-256 neural condicionado ao estado; o baseline atual é unigram e não é capacidade-equivalente.
- O harness mede BPB, perplexidade por byte, latência da avaliação final, throughput efetivo, pico RSS, tempo, distribuição de parâmetros e normas de gradiente. Os resultados existentes continuam sendo smoke tests em fixture sintético e VM de CI.
- Medir as mesmas métricas em corpus real/licenciado e hardware de referência de 4 GB.
- Executar ablações: fast/slow versus RNN simples; segmentação desligada/ligada; hierarquia de uma versus três escalas; pesos fixos versus adaptativos. Essas ablações não são executadas pela matriz atual.
- Repetir experimentos com múltiplas sementes em dados representativos.
- Avaliar dados fora do treino.
- Fazer revisão bibliográfica ampliada antes de qualquer reivindicação de originalidade.

A segmentação da G1 é auxiliar e não altera o caminho preditivo B0. Qualquer alegação de melhoria de BPB ou latência causada por agrupamentos exige uma variante integrada e comparação end-to-end.

## 12.4 Plano de validação

1. Compilar testes unitários de matemática sem iniciar treinamento.
2. Verificar formas e contagem exata de parâmetros.
3. Verificar causalidade e distribuição normalizada dos 256 bytes.
4. Comparar gradientes com diferenças finitas.
5. Validar estado transportado entre blocos e truncamento de gradientes.
6. Validar UTF-8, round-trip, colisões e limites de cache.
7. Validar checkpoint, retomada e isolamento de sessões.
8. Smoke test sintético do pipeline de treinamento e benchmark executado em CI; estabilidade em corpus real ainda não validada.
9. Executar comparações e ablações controladas em corpus licenciado e hardware de referência.
10. Registrar relatório reproduzível com dados, hardware, compilador, configuração e métricas.

## 12.5 Regra de status

- **Aprovado:** decisão de alto nível escolhida.
- **Especificado:** contratos e padrões documentados.
- **Implementado:** código existente.
- **Testado:** testes executados com resultado registrado.
- **Validado:** resultado comparado e aceito com evidência.

A especificação documental da G1 está encerrada. As etapas de infraestrutura, treino, checkpoint, benchmark básico, geração e instrumentação de avaliação estão implementadas e cobertas por testes. A validação científica ainda depende de corpus real/licenciado, baseline neural comparável, ablações arquiteturais e hardware de referência. Não confundir implementação de instrumentação com comprovação de qualidade linguística.
