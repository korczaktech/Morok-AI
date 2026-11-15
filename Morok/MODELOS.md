# Especificação dos modelos Morok AI

## Organização por geração

G1 e G3 são as gerações atualmente mantidas. Os perfis disponíveis são definidos individualmente por geração; não é obrigatório que todas tenham os mesmos perfis.

```text
Morok-AI/
├── Aplicativo/
├── Site/
└── Morok/
    ├── include/                 # contratos e infraestrutura pública compartilhada
    ├── src/                     # implementações do Core compartilhado
    ├── Gerações/
    │   ├── G1/
    │   │   ├── shared/          # motor e subsistemas exclusivos da G1
    │   │   ├── Mokosh/
    │   │   ├── Tsukuyomi/
    │   │   ├── Apollo/
    │   │   └── TsukuyomiMax/
    │   └── G3/                  # estrutura documental
    ├── apps/
    ├── data/
    ├── docs/
    └── tests/
```

As pastas públicas da raiz abrigam somente capacidades realmente compartilhadas. Código exclusivo, parâmetros, configurações, memória especializada, agentes, treinamento, inferência e testes pertencem ao respectivo modelo, conforme a necessidade real. Não duplicar código público dentro das gerações.

## Modelos

### Mokosh — eficiência e baixo consumo
Modelo orientado a uma execução mais calma e econômica. Prioriza reduzir memória, processamento e energia, mantendo qualidade suficiente para a tarefa. Deve evitar agentes e operações adicionais sem benefício mensurável.

### Tsukuyomi ⚫ — agente único
Modelo de identidade visual escura/preta. Opera com exatamente um agente. Não delega tarefas a agentes adicionais nem forma equipes de agentes; prioriza execução individual e autocontida.

### Artemis — arquitetura multiagente
Modelo destinado a tarefas que se beneficiam de colaboração. Deve utilizar mais de dois agentes, portanto no mínimo três agentes ativos na arquitetura de execução. Coordenação, divisão de tarefas e consolidação de resultados pertencem à implementação exclusiva de Artemis.

### Tsukuyomi Max — rede neural unificada da G1
Modelo neural central da geração G1. Utiliza um único conjunto de parâmetros e um checkpoint próprio para aprender os diferentes tipos de pergunta e diálogos da geração, sem separar a rede por perfil de tarefa. Os demais perfis continuam disponíveis como configurações/modelos independentes para experimentação e comparação.

### Apollo — baixa latência
Modelo orientado à velocidade de resposta. Prioriza reduzir o tempo até a primeira resposta e o tempo total de inferência. Pode usar mais recursos quando isso produzir redução de latência medida e reproduzível.

## Regras de isolamento

1. Código exclusivo de um modelo fica dentro da pasta desse modelo e não pode ser importado por outro.
2. Um modelo não depende de diretórios privados de outra geração ou modelo.
3. Componentes reutilizáveis ficam nas bibliotecas públicas da raiz, como `core/` e `tools/`, ou em outras bibliotecas públicas explicitamente aprovadas.
4. Uma biblioteca pública não pode conter lógica específica de Mokosh, Tsukuyomi, Tsukuyomi Max, Artemis ou Apollo.
5. Cada perfil mantém suas políticas, adaptadores, configurações e testes próprios. Parâmetros, treinamento, inferência e memória podem ser compartilhados no nível da geração quando fizerem parte do mesmo motor; um perfil nunca importa código privado de outro perfil.
6. Não criar diretórios ou subsistemas que o modelo não utiliza.
7. O CMake deve oferecer alvos independentes por modelo. Compilar um modelo não compila fontes privadas dos demais.
8. Testes de isolamento verificam que os alvos não incluem fontes privadas de outros modelos.
9. Checkpoints e artefatos identificam geração e modelo; compartilhamento exige conversor explícito, versionado e testado.
10. Evoluções posteriores não podem obrigar gerações anteriores a importar funcionalidades mais avançadas.

## Relação entre gerações

Os modelos são definidos por geração. A G1 possui somente Tsukuyomi Max. A quantidade de parâmetros, mecanismos cognitivos e capacidades será definida em especificação própria. Modelos homônimos de gerações diferentes não precisam ter a mesma arquitetura interna.

## Organização real do código

O diretório público `include/` e a biblioteca `Morok::Core` contêm contratos e infraestrutura neutra, usados por mais de uma geração. Não devem conter parâmetros, forward pass, treinamento, geração, checkpoints ou adaptadores específicos de uma geração/modelo.

A implementação compartilhada da geração G1 está em `Gerações/G1/shared/` e é compilada apenas em `Morok::G1`. Inclui o motor de parâmetros, forward pass, BPTT, otimizador, treinamento, inferência, checkpoints e adaptadores de linguagem/comunicação/aprendizado que ainda são específicos da G1.

Aplicações, scripts, dados de reforço, fixtures e testes específicos da geração mantida ficam dentro de `Gerações/G1/`. O CMake fica em `Morok/CMakeLists.txt`, na raiz do diretório do projeto, como orquestrador de build.

## Estado dos perfis

G1 possui somente o alvo CMake de Tsukuyomi Max. O modelo mantém suas políticas e implementação exclusiva em `Gerações/G1/TsukuyomiMax/`; o motor e os adaptadores compartilhados permanecem em `Gerações/G1/shared/`.

G3 ainda não possui motor executável, treinamento ou inferência. Seus diretórios são apenas estrutura documental. Não há dependência de outras gerações dentro da G3.

A organização mantém as implementações específicas dentro de cada geração, os perfis dentro de seus diretórios, e a infraestrutura realmente comum na raiz. A validação local de compilação e testes deve ser executada antes de considerar a migração encerrada.

## Modelo removido
Hephaestus não faz parte da arquitetura aprovada.
