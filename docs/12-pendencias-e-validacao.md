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
4. Autodiferenciação ou gradientes manuais, com justificativa de custo e verificações por diferenças finitas.
5. Inventário automático dos 1.000 parâmetros.
6. Formato de corpus e carregamento incremental.
7. Formato binário de checkpoint e testes de corrupção.
8. Testes unitários, integração e benchmark.
9. Implementação do preditor baseline de frequência e do baseline softmax comparativo.
10. Implementação do núcleo fast/slow, pontuador e segmentador.

## 12.3 Pendências de validação experimental

- Avaliar se d=11 oferece estabilidade e capacidade úteis.
- Comparar BPTT 32/64/128 e learning rates definidos no documento 18.
- Comparar AdamW e SGD com momentum.
- Comparar fatoração binária com softmax de 256 classes.
- Medir BPB, latência, throughput, RAM e tempo de treinamento.
- Executar ablações: fast/slow versus RNN simples; segmentação desligada/ligada; hierarquia de uma versus três escalas; pesos fixos versus adaptativos.
- Repetir experimentos com múltiplas sementes.
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
8. Treinar baseline pequeno e verificar estabilidade.
9. Executar comparações e ablações controladas.
10. Registrar relatório reproduzível com dados, hardware, compilador, configuração e métricas.

## 12.5 Regra de status

- **Aprovado:** decisão de alto nível escolhida.
- **Especificado:** contratos e padrões documentados.
- **Implementado:** código existente.
- **Testado:** testes executados com resultado registrado.
- **Validado:** resultado comparado e aceito com evidência.

A especificação documental da G1 está encerrada; implementação, testes e validação continuam pendentes. Não confundir esses estados.
