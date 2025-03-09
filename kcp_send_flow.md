# KCP数据发送流程图

本图展示了KCP协议的数据发送流程，从应用层发起请求到通过UDP传输的完整过程。

## 数据发送流程

```mermaid
flowchart TD
    %% 样式定义
    classDef applicationLayer fill:#a5d6a7,stroke:#2e7d32,stroke-width:2px,color:black,rx:5px
    classDef kcpLayer fill:#e3f2fd,stroke:#1565c0,stroke-width:2px,color:black,rx:5px
    classDef udpLayer fill:#ffcc80,stroke:#ef6c00,stroke-width:2px,color:black,rx:5px
    classDef processBox fill:#bbdefb,stroke:#1565c0,stroke-width:1px,color:black,rx:5px
    classDef timerBox fill:#c8e6c9,stroke:#2e7d32,stroke-width:1px,color:black,rx:5px
    classDef decisionBox fill:#ffecb3,stroke:#ff8f00,stroke-width:1px,color:black,rx:5px
    classDef returnBox fill:#f5f5f5,stroke:#9e9e9e,stroke-width:1px,color:black,rx:5px

    %% 节点定义
    subgraph AL["应用层"]
        direction TB
        A1["发送数据请求"] :::timerBox
        A2["定时更新机制"] :::timerBox
    end
    AL:::applicationLayer

    subgraph KL["KCP协议层"]
        direction TB
        K1["数据分片处理"] :::processBox
        K2["加入发送队列"] :::processBox
        K3["检查是否需要刷新"] :::processBox
        K4["判断<br>是否需要刷新"] :::decisionBox
        K5["返回，等待下次更新"] :::returnBox
        K6["执行刷新过程"] :::processBox
        
        %% KCP刷新子流程
        subgraph KF["数据发送过程"]
            direction LR
            F1["移动数据到<br>发送缓冲区"] :::processBox
            F2["发送确认包"] :::processBox
            F3["发送数据包"] :::processBox
        end
    end
    KL:::kcpLayer

    %% UDP层
    U1["UDP底层传输"] :::udpLayer

    %% 连接关系
    A1 --> K1
    K1 --> K2
    A2 --> K3
    K3 --> K4
    K4 -- "否" --> K5
    K4 -- "是" --> K6
    K6 --> KF
    F1 --> U1
    F2 --> U1
    F3 --> U1

    %% 注释
    KFC["将数据转换为KCP协议包发送出去"]
    KF --- KFC
```

## 流程说明

1. **应用层流程**：
   - 应用程序发起数据发送请求
   - 独立的定时更新机制周期性触发检查

2. **KCP协议层处理**：
   - 接收到应用层数据后，进行数据分片
   - 分片后的数据包加入发送队列
   - 定时器触发时，检查是否需要刷新状态
   - 根据条件判断是否执行刷新过程

3. **数据发送过程**：
   - 将发送队列的数据移入发送缓冲区
   - 发送确认包（ACK）
   - 发送数据包

4. **底层传输**：
   - 通过UDP协议发送所有数据包

这个流程体现了KCP协议的核心特性：采用自定义的可靠传输机制，通过分片、定时检查和主动刷新等方式，在UDP基础上实现可靠传输。 