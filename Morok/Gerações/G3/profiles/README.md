# Perfis da G3

Os quatro perfis são políticas sobre o mesmo backend G3, o mesmo contrato
`ModelBackend`, o mesmo gerador e o mesmo `SessionManager`. Não existem quatro
cópias do Core, quatro motores ou quatro implementações neurais.

Todos carregam o mesmo artefato de arquitetura `0x0302`, com 100.000
parâmetros. As diferenças abaixo são políticas de execução e apresentação,
não pesos independentes nem capacidades linguísticas comprovadamente distintas.

| Perfil | Agentes | Temperatura | Top-K | Entrada máx. | Saída máx. | Sessões recomendadas |
|---|---:|---:|---:|---:|---:|---:|
| Mokosh | 1 | 0,55 | 4 | 2.048 B | 192 B | 8 |
| Tsukuyomi | 1 | 0,70 | 10 | 4.096 B | 512 B | 16 |
| Artemis | 3 | 0,70 | 10 | 4.096 B | 384 B/agente | 48 |
| Apollo | 1 | 0,00 | 1 | 2.048 B | 256 B | 16 |

## Mokosh

Perfil de baixo consumo. Limita o contexto, o tamanho de resposta, o Top-K e
a capacidade recomendada do gerenciador de sessões. O backend e os pesos ainda
são os mesmos 100 mil parâmetros; portanto, a redução de custo é por requisição
e por concorrência, não uma redução do tamanho do modelo carregado.

## Tsukuyomi

Identidade de apresentação escura, exposta como metadado `presentation_theme=dark`.
Executa exatamente um agente por requisição. Mantém os limites gerais do G3 e
não cria processos ou modelos auxiliares.

## Artemis

Executa três agentes independentes por requisição. Cada agente usa o mesmo
backend somente leitura, mas recebe ID de sessão, estado recorrente e RNG
independentes. A API retorna três respostas separadas e não as funde ou escolhe
uma vencedora automaticamente. A capacidade recomendada de 48 sessões comporta
16 requisições simultâneas com três agentes cada.

## Apollo

Perfil de baixa latência. Usa temperatura zero e Top-K 1 (seleção greedy),
com limites menores de entrada/saída e timeout de dois segundos. O objetivo é
reduzir trabalho de amostragem e limitar o tamanho da geração; a latência real
depende do hardware e deve ser medida.

## API C++

Use `profile_config(ProfileId)` para obter a política e
`run_profile(backend, sessions, id, request_id, prompt)` para executar. O
chamador continua dono do backend e do `SessionManager`. A capacidade do
gerenciador deve ser configurada usando `recommended_session_capacity`.

Artemis retorna três itens em `std::vector<AgentResponse>`; os outros perfis
retornam exatamente um. O identificador de sessão é derivado do ID da requisição,
do perfil e do índice do agente. Não reutilize IDs de requisição para conversas
independentes.

## CLI

```sh
morok-g3-generate modelo.morok --profile=mokosh "pergunta"
morok-g3-generate modelo.morok --profile=tsukuyomi "pergunta"
morok-g3-generate modelo.morok --profile=artemis "pergunta"
morok-g3-generate modelo.morok --profile=apollo "pergunta"
```

O perfil padrão é Tsukuyomi. As opções explícitas de temperatura, Top-K e
limite de saída podem sobrescrever os valores do perfil. O benchmark usa a
política selecionada.

## Limites

- Os quatro perfis ainda não têm checkpoints/pesos próprios.
- Artemis executa agentes independentes, mas não tem agregador, votação,
  planejamento ou síntese multiagente.
- O tema escuro é metadado para a camada de apresentação; o CLI não possui UI.
- Top-K 4 do Mokosh pode prejudicar bytes raros/Unicode; precisa ser avaliado
  com dados representativos antes de ser adotado como configuração universal.
- A quantidade de sessões é uma capacidade recomendada, não uma garantia de RAM.
