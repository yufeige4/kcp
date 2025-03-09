# KCP数据接收流程图

本图展示了KCP协议的数据接收流程，从UDP底层接收数据到应用层获取完整数据的处理过程。

## 数据接收流程

```mermaid
flowchart TD
    %% 样式定义
    classDef applicationLayer fill:#a5d6a7,stroke:#2e7d32,stroke-width:2px,color:black,rx:5px
    classDef kcpLayer fill:#e3f2fd,stroke:#1565c0,stroke-width:2px,color:black,rx:5px
    classDef udpLayer fill:#ffcc80,stroke:#ef6c00,stroke-width:2px,color:black,rx:5px
    classDef processBox fill:#bbdefb,stroke:#1565c0,stroke-width:1px,color:black,rx:5px
    classDef userBox fill:#c8e6c9,stroke:#2e7d32,stroke-width:1px,color:black,rx:5px
    classDef decisionBox fill:#ffecb3,stroke:#ff8f00,stroke-width:1px,color:black,rx:5px
    classDef returnBox fill:#f5f5f5,stroke:#9e9e9e,stroke-width:1px,color:black,rx:5px

    %% 节点定义
    subgraph UL["UDP层"]
        U1["收到UDP数据包"] :::udpLayer
    end

    subgraph KL["KCP协议层"]
        K1["接收数据并处理"] :::processBox
        K2["解析数据包类型"] :::processBox
        K3{"数据包类型判断"} :::decisionBox
        
        %% 数据包处理分支
        subgraph DP["数据包(PUSH)处理"]
            P1["加入接收缓冲区"] :::processBox
            P2["发送确认包(ACK)"] :::processBox
            P3["移动有序数据到接收队列"] :::processBox
        end
        
        %% ACK包处理分支
        subgraph AP["确认包(ACK)处理"]
            A1["标记对应数据为已确认"] :::processBox
            A2["更新RTT计算"] :::processBox
            A3["从发送缓冲区移除已确认数据"] :::processBox
        end
        
        %% 窗口控制包处理分支
        subgraph WP["窗口包处理"]
            W1["更新远端窗口大小"] :::processBox
        end
    end
    KL:::kcpLayer

    subgraph AL["应用层"]
        A4["请求接收数据"] :::userBox
        A5["接收完整数据"] :::userBox
        A6["分片数据重组"] :::processBox
    end
    AL:::applicationLayer

    %% 连接关系
    U1 --> K1
    K1 --> K2
    K2 --> K3
    K3 -- "数据包" --> DP
    K3 -- "确认包" --> AP
    K3 -- "窗口包" --> WP
    P3 --> A4
    A4 --> A6
    A6 --> A5

    %% 注释说明
    DPC["接收数据包并发送确认"]
    DP --- DPC
    
    APC["处理对方的确认信息"]
    AP --- APC
    
    WPC["更新流量控制参数"]
    WP --- WPC
```

## 流程说明

1. **UDP层数据接收**：
   - 底层UDP协议收到数据包并上交到KCP协议层

2. **KCP协议层处理**：
   - 接收并解析UDP数据包
   - 根据KCP协议头信息，将数据包分为三类处理:
     - **数据包(PUSH)处理**：收到的是对方发来的数据
       - 将数据加入接收缓冲区
       - 发送确认包(ACK)告知对方已收到
       - 将按序的数据从缓冲区移入接收队列
     - **确认包(ACK)处理**：收到的是对方对我方数据的确认
       - 标记相应数据包为已确认状态
       - 更新RTT(往返时间)计算
       - 从发送缓冲区中移除已确认数据
     - **窗口包处理**：收到的是窗口控制信息
       - 更新对方通告的窗口大小

3. **应用层获取数据**：
   - 应用程序请求接收数据
   - KCP将分片数据重组为完整数据包
   - 返回给应用层完整数据

这个流程体现了KCP协议在数据接收处理方面的核心机制：可靠性保证通过确认包实现，顺序性保证通过队列管理实现，流量控制则通过窗口大小调整实现。 