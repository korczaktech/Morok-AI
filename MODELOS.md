# Especificação dos modelos Morok AI

## Organização por geração

Cada geração contém quatro modelos especializados. G1, G2 e G3 são gerações; Mokosh, Tsukuyomi, Artemis e Apollo são modelos dentro de cada geração.

```text
Morok-AI/
├── include/                 # contratos e infraestrutura pública compartilhada
├── src/                     # implementações do Core compartilhado
├── G1/
│   ├── shared/              # motor e subsistemas exclusivos da geração G1
│   ├── Mokosh/
│   ├── Tsukuyomi/
│   ├── Artemis/
│   └── Apollo/
├── G2/
│   ├── engine/              # motor de especialistas exclusivo da G2
│   ├── model/               # composição do modelo G2
│   ├── Mokosh/
│   ├── Tsukuyomi/
│   ├── Artemis/
│   └── Apollo/
├── G3/                      # estrutura documental; sem motor implementado
│   ├── Mokosh/
│   ├── Tsukuyomi/
│   ├── Artemis/
│   └── Apollo/
├── apps/                    # utilitários realmente compartilhados
├── data/                    # dados comuns e manifests
├── docs/
└── tests/                   # testes do Core e contratos compartilhados
```

As pastas públicas da raiz abrigam somente capacidades realmente compartilhadas. Código exclusivo, parâmetros, configurações, memória especializada, agentes, treinamento, inferência e testes pertencem ao respectivo modelo, conforme a necessidade real. Não duplicar código público dentro das gerações.

## Modelos

### Mokosh — eficiência e baixo consumo
Modelo orientado a uma execução mais calma e econômica. Prioriza reduzir memória, processamento e energia, mantendo qualidade suficiente para a tarefa. Deve evitar agentes e operações adicionais sem benefício mensurável.

### Tsukuyomi ⚫ — agente único
Modelo de identidade visual escura/preta. Opera com exatamente um agente. Não delega tarefas a agentes adicionais nem forma equipes de agentes; prioriza execução individual e autocontida.

### Artemis — arquitetura multiagente
Modelo destinado a tarefas que se beneficiam de colaboração. Deve utilizar mais de dois agentes, portanto no mínimo três agentes ativos na arquitetura de execução. Coordenação, divisão de tarefas e consolidação de resultados pertencem à implementação exclusiva de Artemis.

### Apollo — baixa latência
Modelo orientado à velocidade de resposta. Prioriza reduzir o tempo até a primeira resposta e o tempo total de inferência. Pode usar mais recursos quando isso produzir redução de latência medida e reproduzível.

## Regras de isolamento

1. Código exclusivo de um modelo fica dentro da pasta desse modelo e não pode ser importado por outro.
2. Um modelo não depende de diretórios privados de outra geração ou modelo.
3. Componentes reutilizáveis ficam nas bibliotecas públicas da raiz, como `core/` e `tools/`, ou em outras bibliotecas públicas explicitamente aprovadas.
4. Uma biblioteca pública não pode conter lógica específica de Mokosh, Tsukuyomi, Artemis ou Apollo.
5. Cada perfil mantém suas políticas, adaptadores, configurações e testes próprios. Parâmetros, treinamento, inferência e memória podem ser compartilhados no nível da geração quando fizerem parte do mesmo motor; um perfil nunca importa código privado de outro perfil.
6. Não criar diretórios ou subsistemas que o modelo não utiliza.
7. O CMake deve oferecer alvos independentes por modelo. Compilar um modelo não compila fontes privadas dos demais.
8. Testes de isolamento verificam que os alvos não incluem fontes privadas de outros modelos.
9. Checkpoints e artefatos identificam geração e modelo; compartilhamento exige conversor explícito, versionado e testado.
10. Evoluções posteriores não podem obrigar gerações anteriores a importar funcionalidades mais avançadas.

## Relação entre gerações

Cada geração implementará os quatro perfis. Mokosh mantém a prioridade de eficiência; Tsukuyomi usa exatamente um agente; Artemis usa mais de dois agentes; Apollo prioriza baixa latência. A quantidade de parâmetros, mecanismos cognitivos e capacidades de cada geração será definida em especificação própria. Modelos homônimos de gerações diferentes não precisam ter a mesma arquitetura interna.

## Organização real do código

O diretório público `include/` e a biblioteca `Morok::Core` contêm contratos e infraestrutura neutra, usados por mais de uma geração. Não devem conter parâmetros, forward pass, treinamento, geração, checkpoints ou adaptadores específicos de uma geração/modelo.

A implementação compartilhada da geração G1 está em `G1/shared/` e é compilada apenas em `Morok::G1`. Inclui o motor de parâmetros, forward pass, BPTT, otimizador, treinamento, inferência, checkpoints e adaptadores de linguagem/comunicação/aprendizado que ainda são específicos da G1.

A implementação da G2 está dentro de `G2/`. `G2/engine/` contém o motor independente de cada especialista; `G2/model/` implementa o modelo composto por dois especialistas. A G2 não importa headers nem símbolos da G1. Os alvos `Morok::G1` e `Morok::G2` dependem do Core, mas não dependem um do outro.

Aplicações, scripts, dados de reforço, fixtures e testes específicos foram movidos para `G1/` ou `G2/`. O CMake continua na raiz como orquestrador de build e só inclui fontes da geração correspondente no alvo correspondente.

## Estado dos perfis

G1 e G2 possuem alvos CMake independentes para Mokosh, Tsukuyomi, Artemis e Apollo. Cada perfil mantém suas políticas e código exclusivo dentro de seu próprio diretório, com testes próprios. Os motores e adaptadores comuns a todos os perfis de uma geração permanecem no diretório interno dessa geração, fora dos diretórios dos perfis. Artemis expõe três agentes; Tsukuyomi expõe exatamente um. Mokosh e Apollo possuem políticas distintas de geração.

G3 ainda não possui motor executável, treinamento ou inferência. Seus diretórios são apenas estrutura documental. Não há dependência de G1 ou G2 dentro da G3.

A migração de diretórios e namespaces mantém as implementações específicas dentro de cada geração, os perfis dentro de seus diretórios, e a infraestrutura realmente comum na raiz. A validação local de compilação e testes deve ser executada antes de considerar a migração encerrada.

## Modelo removido
Hephaestus não faz parte da arquitetura aprovada.
