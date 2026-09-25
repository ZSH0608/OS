---
name: guidebook-structure
description: riscv64-ucore 实验指导书（在线）是什么、给谁用、lab0~lab9+lab0.5+附录整体结构与各自定位
metadata: 
  node_type: memory
  type: reference
  originSessionId: ef874303-4216-4138-9834-589e56c705fe
  modified: 2026-09-22T11:30:45.957Z
---

来源：在线指导书 `http://8.135.34.58/lab2026/_book/`（HonKit 6.2.2 构建，2026 版）。这是 2026-09-22 抓取的完整结构化总结（非凭目录标题猜测，已抓各页正文）。

## 这是什么 / 给谁用
**《riscv64-ucore 操作系统实验指导书》**——清华 ucore 教学操作系统从 x86 移植到 64 位 RISC-V 后配套的 step-by-step 教程，代码基于 bbl-ucore 与原版 ucore_os_lab。核心理念"step by step"：每个 lab 假设已完成前一章，从零一个模块一个模块搭出能跑简单命令行的 OS。

**给谁用**：操作系统课程学生（实验成绩计入课程成绩）。默认环境 Ubuntu + QEMU（模拟 riscv64）+ RISC-V 交叉工具链 + OpenSBI。**2026 版新增 lab0.5，明确这是"AI 协作编程"课程**：用大模型 + AI 编程工具"描述需求→生成代码→验证迭代"，而非手写全部代码。

## 整体结构（按目录真实标题）
- **欢迎页**：仅 stub（仓库说明 + sifive 工具链下载链接）
- **lab0 预备起**（5 节）：ucore 历史溯源 / 指导书结构概览 / Linux 安装与命令 / 常用软件 / 搭建交叉编译+QEMU 实验环境
- **lab0.5 AI 驱动的操作系统实验**（6 节，见下）
- **lab1 比麻雀更小的麻雀**：最小可执行内核 + 启动流程（详见 [[lab1-guidebook-detail]]）
- **lab2 物理内存和页表**：以页为单位管理物理内存、分页、页表、物理内存分配器（探测/分配算法）
- **lab3 断，都可以断**：RISC-V 中断、trap 入口、中断处理、时钟中断、中断开关
- **lab4 进程管理**：虚拟内存管理（多级页表）+ 内核线程管理（PCB、创建/调度内核线程）
- **lab5 用户程序**：用户进程、系统调用、首次进入用户态、进程退出
- **lab6 进程调度**：进程状态、进程切换、RR 调度、Stride 调度
- **lab7 同步互斥**：信号量、管程
- **lab8 文件系统**：VFS、SFS、设备、open/read 系统调用、用户程序加载与 shell
- **lab9 页面置换与内存映射**：缺页处理、FIFO 页面置换、mmap 内存映射
- **附录**：makefile 简介、【原理】进程属性与特征解析、【原理】用户进程的特征

## lab0.5 六节内容（AI 协作，重要）
1. **0_environment_setup 环境准备**：厘清"大模型=思考 / API=通信 / AI工具=落实"；不绑定模型（DeepSeek/Claude/GPT/Kimi 均可）；API Key 像密码保管、勿入仓库；先装 Node.js(18+)+Git；四种入口=终端 Agent(Claude Code/Codex)、VS Code 插件(Cline/Continue)、桌面应用、浏览器 Agent(DeepSeek Harness)；可用 ccswitch 统一管多模型配置；环境验证三测试；两个习惯=Git 保护 + 控制 Agent 权限。
2. **1_paradigm_shift 范式转变**：重心从"怎么做"→"做什么"；角色从"实现者"→"设计者"（像建筑师画图纸，AI 是施工队）；需需求分析/系统思维/验证/迭代四种能力；但 OS 核心概念不变，不理解的概念无法写清提示词。
3. **2_prompt_engineering 提示词工程**：反例=只给函数名 / 只给算法步骤（照伪代码翻译没价值）；正例=描述"需要满足什么"而非"怎么实现"；核心原则=基于任务而非函数、边界清楚、所有要求可验证、保留真实语义。
4. **3_prompt_structure 提示词结构**：四段式标准框架 `[PROMPT]`（做什么/改哪里/怎么改）、`[RELY]`（可依赖的宏/结构/函数）、`[GUARANTEE]`（必须交付的函数列表）、`[SPECIFICATION]`（每个函数行为规格，Pre/Post-Condition + Success/Failure case）。
5. **4_practice_guide 实践指南**：五步工作流=理解任务→梳理需求→写提示词→生成验证→迭代优化；AI 不猜意图，你才是最终负责人；提示词与代码要同步。

## ⚠️ 一处章节错位（重要提醒）
lab0「概览」页有一段"lab1~lab9 各解决什么问题"的一句话清单，写的是**经典 x86 ucore 的问题映射，与实际章节整体错位一格**（如它说 lab7=调度、lab9=文件，但实际 lab7=同步互斥、lab9=页面置换与内存映射）。**以目录真实标题 + 各 lab 自己的「实验内容」页为准**（本备忘表格即按真实标题整理）。这印证了站点仍在更新、章节结构改过。

相关：[[lab1-guidebook-detail]]、[[lab1-task-checklist]]、[[lab-environment]]
