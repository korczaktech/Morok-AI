# 3. Sete subsistemas cognitivos

O sistema cognitivo será organizado em sete subsistemas. Eles representam responsabilidades de produto e coordenação, não necessariamente sete redes neurais.

## 3.1 IA-CORE — Núcleo

Responsável por:
- inicialização e encerramento;
- ciclo de vida;
- orquestração dos subsistemas;
- configuração global;
- tratamento de falhas;
- observabilidade e estado do sistema.

## 3.2 IA-LANG — Linguagem

Responsável por:
- entrada e saída linguística;
- codificação e interpretação;
- representação de unidades;
- integração com a RLAH;
- reconstrução e validação de texto.

## 3.3 IA-MEM — Memória

Responsável por:
- memória de curto prazo;
- memória de longo prazo;
- persistência;
- indexação e recuperação;
- controle de contexto;
- políticas de retenção.

A memória externa não será contabilizada como parâmetros treináveis do modelo.

## 3.4 IA-REAS — Raciocínio

Responsável por:
- operações lógicas;
- resolução estruturada de problemas;
- decomposição de tarefas;
- planejamento;
- avaliação de alternativas;
- registro de justificativas operacionais.

A capacidade de raciocínio não será presumida apenas pela existência deste módulo; deverá ser testada por tarefas específicas.

## 3.5 IA-LEARN — Aprendizado

Responsável por:
- treinamento;
- atualização de parâmetros;
- avaliação;
- experimentos;
- otimização;
- checkpoints;
- reprodutibilidade.

## 3.6 IA-COMM — Comunicação

Responsável por:
- interface de interação;
- entrada e apresentação de mensagens;
- sessões;
- formatação de respostas;
- adaptação aos canais de comunicação.

## 3.7 IA-TOOLS — Ferramentas

Responsável por:
- integração com ferramentas;
- execução controlada de operações;
- validação de permissões;
- isolamento de operações;
- registro de resultados;
- tratamento de erros externos.

## 3.8 Regras de integração

- IA-CORE coordena, mas não deve absorver a lógica interna de todos os módulos.
- IA-LANG encapsula detalhes da representação linguística.
- IA-MEM mantém persistência separada do estado transitório do modelo.
- IA-REAS pode solicitar inferências e ferramentas por interfaces definidas.
- IA-LEARN não deve modificar parâmetros durante uma inferência comum, salvo quando um modo de adaptação for formalmente especificado.
- IA-TOOLS deve operar com permissões explícitas.
- IA-COMM não deve conter lógica de treinamento.

## 3.9 Critérios de validação

Cada subsistema deverá ter testes unitários e testes de integração. Falhas em um subsistema deverão ser detectáveis sem provocar corrupção silenciosa dos demais.
