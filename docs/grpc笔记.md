
grpc的基本架构如图：

![grpc架构](image.png)

- 服务端需要继承pb生成的Service类，即服务器骨架，并override在proto中给出的接口函数，作为rpc服务端接受请求后实际执行的代码（callee）
- 客户端需要持有一个stub，在建立channel（连接到目标服务器）后，通过stub来发起调用（caller）
- Request和Response由用户在proto文件中定义