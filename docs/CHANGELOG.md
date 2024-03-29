# 2023.12.13 (Blattvorhang)
基本思路：
注意max, sum, sort三个操作都可以进行拆分，由于不涉及修改操作，直接使用二分法来分配算力。

关键点：  
1. 拆分后的合并
   1. max和sum的合并都很直观，sort采用mergeSort的merge操作来实现。
2. 该拆多少，每部分要拆到哪
3. 是否选择GPU（后续考虑）

## CMake的构建
```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## 文件执行
```bash
./SpeedUp_Computing
```

## 修改内容
建立项目基本文件架构，CMakeLists.txt设置完成，建立两个bash文件，其中`build.sh`用于编译构建可执行文件，`run.bash`用于执行生成的文件。

文件说明：
```shell
.
├── CMakeLists.txt      # 项目构建文件
├── LICENSE             # MIT License
├── README.md
├── build               # 编译生成文件夹，.gitignore已忽略
├── build.bash          # 编译脚本
├── docs
│   └── CHANGELOG.md    # 开发日志
├── include             # 头文件
│   └── calculation.h
├── run.bash            # 运行脚本
└── src                 # CPP源文件
    ├── common.cpp      # 公用函数
    ├── main.cpp        # 主函数
    ├── original.cpp    # 加速前
    └── speedup.cpp     # 加速后
```

添加了宏函数，表示对数据的访问：
```cpp
#define ACCESS(data) log(sqrt(data))
```

# 2023.12.14 (Blattvorhang)
完成了`original.cpp`，区分`client`与`server`，`client`发出信号（暂定为数组起始下标）给`server`，`server`计算完成后把结果发回`client`。
三种运行模式：
1. LOCAL
2. CLIENT
3. SERVER

模式选择由调用可执行文件时的命令行参数给出。

目前能想到的优化方案：
1. 多线程：对数组采用二分，每二分一次产生两个线程，由于`MAX_THREADS`为64，递归深度为5（1+2+4+8+16+32=63）
2. SSE（单指令多数据）：可能要提前进行代码框架设计
3. CUDA：和SSE类似，要提前进行代码框架设计


# 2023.12.15 (Blattvorhang)
求和变量`sum_value`如果选用`float`会产生精度误差，串行和并行结果不一致，其中一次结果如下：
```
Running in local mode.

--- Original version ---
Time consumed: 5.62687s
sum: 2.68435e+08
max: 9.33377

--- Speedup version ---
Time consumed: 0.753798s
sum: 1.12936e+09
max: 9.33377

Speedup ratio: 7.46469
```
由于`log(sqrt(data))`的输入和返回参数都是`double`类型，主要耗时在这两个函数而非加法上，故加法采用`double`并不会产生较大的时间损失。
`sum_value`改用`double`类型后，结果如下：
```
Running in local mode.

--- Original version ---
Time consumed: 5.56075s
sum: 1.13072e+09
max: 9.33377

--- Speedup version ---
Time consumed: 0.704421s
sum: 1.13072e+09
max: 9.33377

Speedup ratio: 7.89407
```
可以发现用时差别不大（好像还变快了，怀疑是前面`float`转`double`花了额外的时间），而加速前后的结果不再有差别。

# 2023.12.16 (Blattvorhang)
并行化归并排序思路：
递归地把数组拆成`MAX_THREADS`份进行归并排序，随后两两合并，排序和合并均并行执行。考虑到合并过程也占用线程，应该拆成`MAX_THREADS / 2`份。
实测CPU跑满，结果如下：
```
--- Original version ---
sum time consumed: 1.3555s
max time consumed: 1.49854s
sort time consumed: 108.965s
Time consumed: 111.82s
sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup version ---
sum time consumed: 0.295863s
max time consumed: 0.314429s
sort time consumed: 23.6107s
Time consumed: 24.2212s
sum: 1.13072e+09
max: 9.33377
Result is sorted.

Speedup ratio: 4.61661
```
加速比并未达到理想中的7左右，怀疑是归并排序复制数据部分占用了太多时间，可以考虑更换排序算法。

于是我测试性地把自己写的归并排序改成了`std::sort`，分成64个部分并行地排序，最后合并起来，结果如下：
```
--- Original version ---
sum time consumed: 1.2942s
max time consumed: 1.44973s
sort time consumed: 103.683s
Time consumed: 106.427s
sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup version ---
sum time consumed: 0.227853s
max time consumed: 0.282442s
sort time consumed: 2023.12.6314s
Time consumed: 13.1418s
sum: 1.13072e+09
max: 9.33377
Result is sorted.

Speedup ratio: 8.0983
```
所以确实可以考虑在排序上面下功夫。

# 2023.12.17 (Blattvorhang)
对`merge`过程做了优化。由于是并行地对数组进行排序，最后还要有一个`merge`过程把子数组合并为一整个有序的数组，这个过程涉及大量数据的复制，之前是用一个`temp`数组来存，先合并到`temp`中，再写回`arr`。举例如下：
```
把一个数组A拆分为八个部分：
A1 A2 A3 A4 A5 A6 A7 A8
分别对每个部分排好序后，合并到数组B中：
B12 B34 B56 B78
接下来把B复制到A中：
A12 A34 A56 A78
随后再把A继续合并到B：
B1234 B5678
接下来把B复制到A中：
A1234 A5678
把A合并到B：
B12345678
把B复制到A中：
A12345678
```
上面的例子充分说明了时间的浪费，故现在考虑两个数组`arr1`和`arr2`，交错地来作为存结果的数组，即：
```
把一个数组A拆分为八个部分：
A1 A2 A3 A4 A5 A6 A7 A8
分别对每个部分排好序后，合并到数组B中：
B12 B34 B56 B78
接下来把B合并到A中：
A1234 A5678
把A合并到B中：
B12345678
最后一步把B复制回A即可：
A12345678
```
可以发现，先前需要6步的合并过程，现在优化到只需要4步了，对于`MAX_THREAD = 64`的情况，带来的优化只会更加明显。但是需要注意什么情况下需要把B复制回A，什么情况不需要，这需要对层数`level`的奇偶性进行判断。

优化后的数据如下：
```
--- Original version ---
Sum time consumed: 1.86367
Max time consumed: 2.23014
Sort time consumed: 153.145
Total time consumed: 157.239

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup version ---
Sum time consumed: 0.283588
Max time consumed: 0.310064
Sort time consumed: 25.9752
Total time consumed: 26.5689

sum: 1.13072e+09
max: 9.33377
Result is sorted.

Sum speedup ratio: 6.57173
Max speedup ratio: 7.19251
Sort speedup ratio: 5.89581
Total speedup ratio: 5.91816
```

# 2023.12.20 (Blattvorhang)
开启两个终端，一个以client运行，一个以server运行，用回环地址`127.0.0.1:8080`连接成功。

# 2023.12.21 (Blattvorhang)
把服务器的IP地址和端口号都放到了命令行参数中，避免反复编译浪费时间。包含错误处理。
```
Usage: [-l | --local] [-c | --client <server_ip> <server_port>] [-s | --server <server_port>]
```

# 2023.12.25 (Blattvorhang)
2023.12.16中提到的`std::sort`比我写的`mergeSort`快的问题，忽略了一个点，`std::sort`排序是对数组的数据本身进行访问，而我的`mergeSort`访问时还加了`ACCESS`宏函数，人为拖慢了时间。将`log(sqrt())`暂时去掉后，重新进行比较，得到的结果大致如下：

| Algorithm  |  ACCESS(data) | Time Consumed/s |
|:----------:|:-------------:|----------------:|
| std::sort  |     data      |      41.50      |
| mergeSort  |     data      |      52.27      |
|parallelSort|     data      |       7.69      |
| mergeSort  |log(sqrt(data))|     162.60      |
|parallelSort|log(sqrt(data))|      26.25      |

注意，以上测试结果均针对使用同一随机数种子`42`使用`std::shuffle`打乱后的数组，具有可比性。（终于为我的排序算法正名了！）

# 2023.12.27 (Blattvorhag)
本题要求访问数据时，用`log(sqrt(data))`套起来，对于SSE，需要使用两个函数，即`_mm_log_ps`和`_mm_sqrt_ps`，但是`_mm_log_ps`不是SSE的标准函数，属于SVML(Short Vector Math Library)，查阅资料发现GCC不支持SVML，见如下链接：

https://www.coder.work/article/815777

https://www.codenong.com/51796612/

有建议使用ICC。由于换编译器比较麻烦，SSE的使用暂时搁置。

# 2023.12.28 (Blattvorhang)
针对之前提到的问题，`rawFloatData`是一个`float`型数组，但套的函数`log(sqrt(data))`是针对`double`型的，由此想到将其改为`logf(sqrtf(data))`。注意浮点数加法不满足结合律，即

$$(a+b)+c\ne a+(b+c)$$

因此加法使用[Kahan累加算法](https://oi-wiki.org/misc/kahan-summation/)来降低有限精度浮点数序列累加值误差。

同时，成对（并行）求和也减少了累计精度误差，这两种操作保证了`float`类型求和的精度。

为了防止读写冲突，使用OpenMP时需要同时对补偿变量`c`进行规约，即

```cpp
#pragma omp parallel for num_threads(MAX_THREADS) reduction(+:sum_value, c)
```

注意，虽然Kahan算法可以减少舍入误差，但它也会使代码变得更复杂，并可能降低代码的性能。但`float`计算比`double`更快，这一牺牲可以换取更高的速度，且最终求和结果与`double`相同，如下：

```
--- Original version ---
  Sum time consumed: 1.27743
  Max time consumed: 1.48786
 Sort time consumed: 107.956
Total time consumed: 110.721

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup version ---
  Sum time consumed: 0.219453
  Max time consumed: 0.256825
 Sort time consumed: 23.6911
Total time consumed: 24.1673

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup ratio ---
  Sum speedup ratio: 5.82097
  Max speedup ratio: 5.79331
 Sort speedup ratio: 4.55682
Total speedup ratio: 4.58144
```

# 2023.12.29 (Blattvorhang)
修改了一些main函数的输入输出。发现数组不打乱加速比更大，怀疑可能是因为分支预测等，最终提交的报告、答辩等建议分打乱和不打乱两个版本的结果。

# 2023.12.30 (Blattvorhang)
网络通讯的初步想法：把大数组拆成多个块，每个块长`BUFFER_SIZE`，注意到只要每次发送一个有序的小数组就行，不需要关心分块的顺序，因为最后都会统一合并。为了提高传输的效率，用多个端口来传数组，例如8080到8095共16个，多线程发送数据，用队列维护可用（未被挤占）的端口，相当于滑动窗口，实现流量控制。

`server`方只要排好一个块就发送出去，这时从队列中取出一个端口号用于发送。先发送当前块中的数组长度，随后再发数据（因为未必是严格的`BUFFER_SIZE`）。发送完毕后，需要等待`client`回复发送完毕的信号后，将端口号重新加入队列，表示可用。注意TCP连接的建立应该在一开始，对队列的所有操作都要加锁！

`client`方每收到两个块就合并一次，逐层往上累加（这里逐层合并的算法细节再思考一下，本地排序每次取最短的两个数组合并？可能需要考虑用优先队列，维护一个类，包含每个块的长度、指针）。接收数据放到数组中的时候，注意加锁保护临界区。

# 2023.12.31 (KevinTung)
删除了main函数中的冗余头文件
新增了cuda.cu和cuda.cuh两个文件，用于实现基于双调排序的CUDA加速排序算法
使用cuda需要确保输入的排序数字个数为2的幂次，在函数内填充了数据至2的幂次，排序后再删除
增加了cmake文件关于cuda的配置
在common.h中增加了CUDA的宏定义，用于判断是否使用CUDA加速
VMware没有办法配置显卡，暂时搁置，准备换wsl使用

# 2023.12.31 (Blattvorhang)
TCP连接需要确定端口号，虚拟机（WSL和VMWare）默认使用NAT模式，会进行端口映射，导致应用程序的实际端口号与运行时规定的不同，因此要先将虚拟机网络设置改为桥接模式。使用RJ-45网线连接配置为局域网，虚拟机手动指定静态IP地址，使用对应端口连接成功。

将收发数组统一打包成了两个函数`sendArray`和`recvArray`，经过测试可以完整传输整个数组，本地回环地址用时0.2s左右，这样就不需要再考虑通信协议的问题了。

# 2024.1.1 (Blattvorhang)
完成了两台计算机传递sum和max的程序，通过回环地址在本地跑通，结果如下：

```
--- Original version ---
  Sum time consumed: 1.39273
  Max time consumed: 1.59763
Total time consumed: 2.99036

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup version ---
  Sum time consumed: 0.224975
  Max time consumed: 0.232137
Total time consumed: 0.457113

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup ratio ---
  Sum speedup ratio: 6.19058
  Max speedup ratio: 6.88227
Total speedup ratio: 6.54184
```

结果正确。

完成了两台计算机各自跑一半数组并完整传递的代码，本地跑通。

# 2024.1.2 (Blattvorhang)
测试发现，即使是TCP也可能存在丢包现象，需要考虑请求重发的代码。

# 2024.1.5 (Blattvorhang、KevinTung)
用RJ-45六类千兆网线测得2000000*64（约500MB）大小的float数组传输用时为4.6s，带宽约为108.7MB/s。

|SEP_ALPHA|Time Consumed(s)|
|:-------:|:--------------:|
|   0.6   |      16.0      |
|   0.5   |      15.8      |
|   0.4   |      19.5      |

最终决定选用0.5的比例来分割。

# 2024.1.5（KevinTung）
使用wsl配置好了CUDA
编写好了基数排序算法
改写了cuda.cu和cuda.cuh
删除了是否使用cuda的选项
改写了speedup，使用cpu，gpu同时进行排序任务，单机加速比成功到11.3
新增了access.cu和access.cuh，是用cuda模拟log sqrt任务，增加计算量
```
------------------------------
Time test 1/1 begins.

--- Original version ---
  Sum time consumed: 1.12203
  Max time consumed: 1.19147
 Sort time consumed: 126.692
Total time consumed: 129.005

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup version ---
  Sum time consumed: 0.143074
  Max time consumed: 0.132925
 Sort time consumed: 11.0408
Total time consumed: 11.3168

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup ratio ---
  Sum speedup ratio: 7.84235
  Max speedup ratio: 8.96347
 Sort speedup ratio: 11.4749
Total speedup ratio: 11.3995
```

# 2024.1.6（KevinTung）
重写了cuda代码，使用merge排序，速度更快了，代码也相较于基数排序更简单
改进了加速方法：加速比使用0.55，最后一次归并使用omp
```
Time test 1/1 begins.

--- Original version ---
  Sum time consumed: 1.13723
  Max time consumed: 1.20963
 Sort time consumed: 126.87
Total time consumed: 129.217

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup version ---
  Sum time consumed: 0.148605
  Max time consumed: 0.125594
 Sort time consumed: 14.3801
Total time consumed: 14.6543

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup ratio ---
  Sum speedup ratio: 7.65274
  Max speedup ratio: 9.63128
 Sort speedup ratio: 8.82265
Total speedup ratio: 8.81772
```

编写了基数排序的cpu版本，比merge更快一些
现在单机运行时间在10s以内，加速比12，已经比双机理论速度更快了（双机最快：10s/2+5s(千兆网传输所有数据)=10s）
```
--- Original version ---
  Sum time consumed: 1.16718
  Max time consumed: 1.20018
 Sort time consumed: 116.004
Total time consumed: 118.371

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup version ---
  Sum time consumed: 0.159357
  Max time consumed: 0.183435
 Sort time consumed: 9.58962
Total time consumed: 9.93241

sum: 1.13072e+09
max: 9.33377
Result is sorted.

--- Speedup ratio ---
  Sum speedup ratio: 7.32432
  Max speedup ratio: 6.54282
 Sort speedup ratio: 12.0968
Total speedup ratio: 11.9177
```

# 1.15 (Blattvorhang)
最终文件树：
```shell
.
├── CMakeLists.txt      # 项目构建文件
├── README.md
├── build.bash          # 编译脚本
├── docs
│   └── CHANGELOG.md    # 开发日志
├── include             # 头文件
│   ├── common.h
│   ├── cuda.cuh
│   ├── net.hpp         # 网络公用函数及client端、server端函数
│   ├── original.h
│   └── speedup.h
├── run_client.bash     # client模式运行脚本
├── run_local.bash      # local模式运行脚本
├── run_server.bash     # server模式运行脚本
└── src                 # CPP源文件
    ├── client.cpp      # client端函数
    ├── common.cpp      # 公用函数
    ├── cuda.cu         # CUDA
    ├── main.cpp        # 主函数
    ├── net.cpp         # 网络公用函数
    ├── original.cpp    # 加速前
    ├── server.cpp      # server端函数
    └── speedup.cpp     # 加速后
```