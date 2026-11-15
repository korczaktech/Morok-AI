# Especificação dos modelos Morok AI

## Organização por geração

G1, G2 e G3 são gerações distintas. Os perfis e suas implementações são definidos por geração; modelos com o mesmo nome em gerações diferentes não precisam compartilhar arquitetura, parâmetros ou checkpoints.

```text
Morok-AI/
├── Aplicativo/
├── Site/
└── Morok/
    ├── include/                 # contratos e infraestrutura pública compartilhada
    ├── src/                     # implementações do Core compartilhado
    ├── Gerações/
    │   ├── G1/
    │   │   ├── shared/
    │   │   └── TsukuyomiMax/
    │   ├── G2/
    │   │   ├── Apollo/
    │   │   ├── Tsukuyomi/
    │   │   └── Mokosh/
    │   └── G3/
    ├── apps/
    ├── data/
    ├── docs/
    └── tests/
```

As pastas públicas da raiz abrigam somente capacidades realmente compartilhadas. Código exclusivo, parâmetros, configurações, memória especializada, agentes, treinamento, inferência e testes pertencem ao respectivo modelo, conforme a necessidade real. Não duplicar código público dentro das gerações.

## Especificações gerais dos modelos

Os três modelos da G2 representam perfis distintos de capacidade e desempenho. Cada um possui uma quantidade definida de parâmetros:

| Modelo | Perfil | Parâmetros |
|---|---|---:|
| Tsukuyomi | Mais avançado | 10.000 |
| Apollo | Comum / uso geral | 5.000 |
| Mokosh | Rápido | 2.000 |
| **Total da G2** | | **17.000** |

### Tsukuyomi — 10.000 parâmetros

É o modelo de maior capacidade da linha Morok. Assume o papel anteriormente atribuído ao Mokosh: prioriza qualidade, profundidade de raciocínio, capacidade de lidar com tarefas complexas e maior processamento quando necessário. É a opção destinada às tarefas que exigem o máximo de capacidade do sistema, mesmo que isso implique maior custo computacional e latência.

### Mokosh — 2.000 parâmetros

É o perfil otimizado para velocidade e baixa latência. Prioriza respostas rápidas e menor custo computacional, sendo adequado para interações frequentes e tarefas diretas. Não deve ser tratado como o modelo mais avançado: sua prioridade é desempenho de execução.

### Apollo — 5.000 parâmetros

É o perfil geral e equilibrado, destinado ao uso cotidiano. Busca equilíbrio entre capacidade, velocidade e consumo de recursos, sem priorizar extremos de raciocínio ou de velocidade.

### Relação entre os perfis

- **Tsukuyomi:** 10.000 parâmetros; máxima capacidade e tarefas complexas.
- **Apollo:** 5.000 parâmetros; uso geral e equilíbrio.
- **Mokosh:** 2.000 parâmetros; velocidade e baixa latência.

As quantidades acima são especificações de projeto para a G2. Elas definem o orçamento de parâmetros de cada modelo, não comprovam qualidade, velocidade ou capacidade já alcançadas. Arquitetura, distribuição dos parâmetros, orçamento de memória, tempos-alvo e métricas de qualidade devem ser validados experimentalmente. Não presumir que um perfil é superior em todos os indicadores.

### Tsukuyomi Max — implementação histórica da G1

Tsukuyomi Max é o único modelo mantido na G1 e representa a implementação dessa geração. Seu estado e sua arquitetura permanecem definidos pelos documentos específicos da G1. Ele não altera a hierarquia funcional dos três perfis da G2.

## Regras de isolamento

1. Código exclusivo de um modelo fica dentro da pasta desse modelo e não pode ser importado por outro.
2. Um modelo não depende de diretórios privados de outra geração ou modelo.
3. Componentes reutilizáveis ficam nas bibliotecas públicas da raiz, como `core/` e `tools/`, ou em outras bibliotecas públicas explicitamente aprovadas.
4. Uma biblioteca pública não pode conter lógica específica de qualquer modelo.
5. Cada perfil mantém suas políticas, adaptadores, configurações e testes próprios. Parâmetros, treinamento, inferência e memória podem ser compartilhados no nível da geração quando fizerem parte do mesmo motor; um perfil nunca importa código privado de outro perfil.
6. Não criar diretórios ou subsistemas que o modelo não utiliza.
7. O CMake deve oferecer alvos independentes por modelo. Compilar um modelo não compila fontes privadas dos demais.
8. Testes de isolamento verificam que os alvos não incluem fontes privadas de outros modelos.
9. Checkpoints e artefatos identificam geração e modelo; compartilhamento exige conversor explícito, versionado e testado.
10. Evoluções posteriores não podem obrigar gerações anteriores a importar funcionalidades mais avançadas.

## Relação entre gerações

Os modelos são definidos por geração. A G1 possui somente Tsukuyomi Max. A G2 define três perfis: Apollo (comum, 5.000 parâmetros), Mokosh (rápido, 2.000 parâmetros) e Tsukuyomi (mais avançado, 10.000 parâmetros), totalizando 17.000 parâmetros previstos. A G3 permanece documental. Modelos homônimos de gerações diferentes não precisam ter a mesma arquitetura interna.

## Organização real do código

O diretório público `include/` e a biblioteca `Morok::Core` contêm contratos e infraestrutura neutra, usados por mais de uma geração. Não devem conter parâmetros, forward pass, treinamento, geração, checkpoints ou adaptadores específicos de uma geração/modelo.

A implementação compartilhada da geração G1 está em `Gerações/G1/shared/` e é compilada apenas em `Morok::G1`. Inclui o motor de parâmetros, forward pass, BPTT, otimizador, treinamento, inferência, checkpoints e adaptadores de linguagem/comunicação/aprendizado que ainda são específicos da G1.

Aplicações, scripts, dados de reforço, fixtures e testes específicos da geração ficam dentro de sua respectiva pasta. O CMake fica em `Morok/CMakeLists.txt`, na raiz do diretório do projeto, como orquestrador de build.

## Estado dos perfis

G1 possui somente o alvo CMake de Tsukuyomi Max. O modelo mantém suas políticas e implementação exclusiva em `Gerações/G1/TsukuyomiMax/`; o motor e os adaptadores compartilhados permanecem em `Gerações/G1/shared/`.

G2 possui os diretórios de Apollo, Tsukuyomi e Mokosh, com seus orçamentos de parâmetros e funções gerais definidos nesta especificação. A implementação, arquitetura interna, treinamento e inferência ainda devem ser desenvolvidos e documentados individualmente.

G3 ainda não possui motor executável, treinamento ou inferência. Seus diretórios são apenas estrutura documental. Não há dependência de outras gerações dentro da G3.

A organização mantém as implementações específicas dentro de cada geração, os perfis dentro de seus diretórios, e a infraestrutura realmente comum na raiz. A validação local de compilação e testes deve ser executada antes de considerar a migração encerrada.

## Modelo removido

Hephaestus não faz parte da arquitetura aprovada.
