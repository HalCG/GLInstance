# glBlendFuncSeparate（Front-to-Back 短注）

> 完整说明：[FrontToBack混合_src_dst与公式详解.md](./FrontToBack混合_src_dst与公式详解.md)  
> 含：src/dst 定义、四参数逐项对照、A/B/C 数值走查、与 Linked List Over 对比、预乘与溢出。

这两行在 **Blend Pass** 里把当前剥离层叠进 `fboAccum_`，实现 **从近到远（Front-to-Back）Under**：

```422:423:../src/DepthPeelingApp.cpp
    glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO,
                        GL_ONE_MINUS_SRC_ALPHA);
```

| | 本 Pass |
| :--- | :--- |
| **src** | `fboPeel_.color`（本层） |
| **dst** | `fboAccum_.color`（已累积） |
| **RGB** | \(C' = C_{src}\,A_{dst} + C_{dst}\) |
| **Alpha** | \(A' = A_{dst}(1-A_{src})\)（剩余透射） |

必须用 `Separate`：RGB 要 `DST_ALPHA`，Alpha 要 `ONE_MINUS_SRC_ALPHA`，两套因子不能塞进普通 `glBlendFunc`。

Final 里 `background * frontColor.a` 用的就是这里乘出来的剩余透射。
