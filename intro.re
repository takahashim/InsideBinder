= {intro} Binderとシステムサービス

//lead{
別のプロセスに処理を依頼する場合には、何かしらのプロセス間通信（IPC: Inter Process Communication）の仕組みが必要です。
Unixではパイプやシグナル、Unixドメインソケットなどが良く使われますが、AndroidではこれにプラスしてBinderという独自のIPCも使われています。

本章ではBinderについて詳細に扱っていきます。
Binderは通常のプロセス間通信の仕組みよりも用途が限定されています。システムプログラミングで使う、というターゲットの元に設計されています。
その為プロセスを超えて送る事が出来る物も通常のIPCよりも多く、単純な値以外にも、ファイルディスクリプタやある種のオブジェクトなどを送る事が出来ます。
Androidはこの機能をフル活用していて、例えば7章の内容の裏側では、ActivityThreadの内部クラスであるApplicaitonThreadのプロキシをBinderで送ったり、
BundleやActivityRecordなどもBinderで送ったりされていました。
そういった意味ではBinderというのはAndroidにとって重要な技術と言えます。

一方で本章は他の章よりも読者に要求する前提知識が多く、本書の中では少し特殊な章となっています。
そこで最初に8.1で、まあり前提知識が無い読者を想定して、本書の他の章を理解するのに必要な最低限の事、ひいてはAndroidという物を理解するのに必要なBinderの最低限の所を説明する事から始める事にしました。

あまりこの分野に馴染みのない人がどこまで本章を読むのかは、8.1を読んで、自身で判断してもらいたい、と思っています。
8.1の内容さえ理解しておけば、他の章を読むのには差し支えありません。
本章が難しいと思ったら8.1だけ読んだ上で次の章に進んでもらえたら、と思います。

ですが、もし分散オブジェクトにある程度の理解があるなら、本章を最後まで読めばBinderについての全てを理解する事が出来るでしょう。
//}

== 8.1 最低限のBinder基礎知識 - 分散オブジェクトを知らない人向け

Binderは、使うのは簡単だけれど実現する実装が複雑、という物です。
使い方を覚えるのは比較的簡単で、それだけ分かっていれば他の章を理解するのには十分です。

そこで本節では、難解な実現方法は気にせずに、利用者の視点に限定して必要最低限な話をしていきたいと思います。
使う側に関して言えば、以下の三つを抑えておけば十分だと思います。

 * Binderの特徴
 * Binderで送れるもの
 * IBinderとは何か、Stub.ProxyとStub.asInterface

そこで本節では、なるべく専門用語などを出さずに、上記の事を説明してみたいと思います。
逆に分散オブジェクトに詳しくてBinderの詳細に興味がある人は、本節の内容はあとでより詳細に扱うので、流し読みで十分です。
8.2から真面目に読み始めていただけたら、と思います。


=== 8.1.1 Binderの特徴

Binderとはプロセス間通信の仕組みと、その上に構築された分散オブジェクトの仕組みです。
この時点で「プロセス間通信」とか「分散オブジェクト」と言った専門用語が出てきてしまいますね。
この節ではなるべくそういう言葉を使わないで説明してみます。

===[column]  分散オブジェクトシステムとIPC、RPC
分散オブジェクトとは、別のプロセスのオブジェクトを自分のプロセスのオブジェクトのように呼べる技術の事です。
これに関連のある言葉として、IPC(Inter Process Communication、プロセス間通信)、RPC (Remote Procedure Call)があります。

IPCはプロセスの間で何かしら情報をやり取りするもの全般を指します。
この時点ではオブジェクトや関数と言った概念を前提とはしません。UnixのパイプなどもIPCの仕組みと言えるでしょう。

RPCは単なるIPCを越えてプロシージャ、つまり関数を呼び出す、という構造を持っています。
関数を呼び出す場合、相手の関数を識別する方法があって、それを通常の関数呼び出しのように呼べる、という事を意味します。
さらにRPCの場合は関数の引数をシリアライズして送信し、受け取る側でデシアライズする仕組みが含まれるのが普通です。
多くの場合RPCはプロキシの関数を呼び出すと、相手側の実体の関数が呼ばれる、という振る舞いをします。
そして間の部分は何らかの方法の自動生成になる事が多いと思います。
古くはIDLという言語でインターフェースを書いて、そこからコードを自動生成していました。
最近はインターフェースなどの型からリフレクションを用いて実行時に生成する物も多くなっています。

RPCと分散オブジェクトは昨今ではほとんど同じ意味に使われる事が多いですが、
分散オブジェクトの場合はオブジェクトのインスタンスが存在する前提となります。
オブジェクトのインスタンスがあると、一意性の識別と寿命の管理という問題が発生し、
マシンをまたがる前提だといろいろと難しい問題が発生します。
乱暴に言ってしまえば、分散オブジェクトではRPCでは必要無かった、規約に従ったリファレンスカウント処理が必要となる、という事です。

また、オブジェクトが複数存在する結果として、オブジェクトを探す、というのも重要な機能となる事が多いと思います。
マシンを超えてネットワークからオブジェクトを探す、となると、なかなか複雑な仕組みとなります。

Binderの構成要素に照らし合わせると、BinderでIPCの仕組みをつかさどるのがbinderドライバとなります。
その上のBpBinderとBBinderの仕組みでだいたい分散オブジェクトの仕組みは構成されている、と言ってしまって良いと思いますが、
普通はIInterfaceやAIDLのレイヤまでを含めて分散オブジェクトシステム、と言うと思います。
binderドライバは最初から上に載る分散オブジェクトの為に作られているため、
IPCの時点で寿命管理の仕組み(リファレンスカウント)や相手を識別するための仕組み（binder_node）が入っているのが特徴と言えます。
またオブジェクトを探す仕組みとしてservicemanagerがあります。

===[/column]

Binderは別のプロセスのオブジェクトを、自分のプロセス内の通常のオブジェクトと同じように見せかける技術です。
同じような技術はたくさんあるのですが、Binderが少し変わっているのは、最初からシステムプログラミングの用途だけを念頭に作られている事です。
これは一番下の、よそのプロセスとの通信の部分から、一番上の、ユーザーが使うようなライブラリまで一貫しています。
通信部分だけを別の用途で使えるようにもしよう、という配慮も無いですし、上のライブラリを通信部分を差し替えて使えるようにもしよう、とも考えていません。
スタックを上から下まで抱え持って全体を最適化する、というのはGoogleらしいですね。

Binderはシステムプログラミングを前提にして、それ以外には必要のない抽象化は一切行わず、なるべくシンプルな実装となっています。
それを踏まえたBinderのカタログ的な特徴としては、以下のような物になるでしょう。

 * 同一マシン内だけでしか使えない
 ** シンプルなインターフェース
 ** コンパクトな実装
 ** 高パフォーマンスで省メモリ
 * ドライバとしてカーネルにアクセスする事前提
 ** 通信のレイヤでスレッドを認識出来る
 ** 相手のタスク構造体を直接操作出来る
 *** ファイルディスクリプタが送れる

カーネルのドライバでの実装を前提とするので、相手のタスク構造体@<fn>{taskproc}を直接触れる、
というのは珍しい部分に思います。
//footnote[taskproc][Linuxカーネルの用語。プロセスを表すカーネルのデータ構造。 ]


個々の項目の妥当性などを詳細に検証していこうとすると複雑な実装に踏み込まなくてはいけませんが、
この位の印象をそのまま持っていたら、そう大きく実態とは乖離していないと思います。


== 8.1.2 Binderの通常の使われ方 - getSystemService()メソッド

Binderを一般のユーザーが使うのは、9割くらいはシステムサービスを使う時だと思います。

0章で見たように、Androidはシステムサービスから構成されたシステムです。
システムサービスはアプリのプロセスとは別のプロセスで動いているので、
システムサービスの呼び出しは全てBinderを使った呼び出しとなります。

システムサービスを使う具体例を見てみましょう。
例えば現在Activityが表示されているディプレイのサイズを取りたい場合、ActivityやContextに対して

//list[code1][WindowManagerサービスの取得]{
    WindowManager wm = (WindowManager)getSystemService(WINDOW_SERVICE);
    Display disp = wm.getDefaultDisplay();
    ...
//}

などというコードを書くとディスプレイのサイズが取得出来ます。
ディスプレイのサイズに限らず、アカウントマネージャでもオーディオでも、別のプロセスで実現されていそうな機能は、
このgetSystemService()というメソッドで何かを取得して、それをキャストして作業するのがAndroidの基本となっています。
#@# TODO:「何か」を補足（直後の一文にも1ヵ所あり）

上記のコードのうち、wm.getDefaultDisplay()の呼び出しがBinderによるメソッド呼び出しとなります。
見て分かる通り、通常のオブジェクトに対するメソッド呼び出しとコードの上では区別できません。

getSystemService()を使って取得したオブジェクトに対してメソッドを呼び出すのがBinderを使っている例なんだな、とだけ覚えておいてもらえれば十分だと思います。

===[column]  分散オブジェクトに関わりある物達
分散オブジェクトはなかなか複雑なシステムでありながら90年代末期には流行っていた為、割と多くの中年プログラマは良く学んでいます。
一方でwebの時代が来た後にはあまり流行らなくなり、最近のエンジニアだとちょっと使われている事はあるけれど大して知らない、という人も多くなってきた印象です。

90年代の終わりには複合ドキュメント、というのがこれからは流行る、と言われていました。
ようするにWordファイルの中にExcelのグラフを貼って、ワープロソフトの中で表計算ソフトを動かす事でグラフも編集出来るようにする、という奴です。
結局はあんまり使いやすく無い上にワープロに貼りたい物は限られているので、アプリケーションごとに対応してしまう方が良い、という結論になったように思います。

Windowsで複合ドキュメントを実現するために使われていた分散オブジェクト技術がCOMです。
COMはWordやExcelのアプリケーション、Internet ExplorerなどのブラウザをRubyやJScriptといったスクリプト言語から触る為に良く使われている技術に思います。
巨大なアプリケーションを外部のスクリプト言語から操作する、というのは分散オブジェクトの使われ方のうち、現在に至るまで最も成功している使われ方に思います。

また、分散オブジェクトはサーバーサイドの開発でアプリケーションのロジックを作る単位として使う、
という使い方が提案された事もありました。アプリケーションのロジックをマシンを分散させる事で負荷分散をしたり出来るんじゃないか、と。
でも結局はコンポーネント単位では無くでアプリケーションサーバー全体を複製しておく方がてっとりばやく、
効率も良いという結論になり、この目的で分散オブジェクトが使われる事もほとんど無くなりました。

== 8.1.3 Binderで送信できるもの

Androidのソースを読んでいて、Binderに付きあたった時に周辺のコードを理解する上で重要になってくるのは、Binderで何を送信出来るか、という事です。
送信出来るものさえ把握しておけば、それが内部でどのように送信されているかはあまり知らなくても、ソースを読む上では問題ありません。

Binderで送信できる主な物は以下の3つです。

 1. 数値、文字列などの値
 2. ファイルディスクリプタ
 3. サービスオブジェクト(BBinderのサブクラス)@<fn>{prox}

//footnote[prox][実際はサービスプロキシも送る事が出来ます。つまりIBinderのサブクラスを送る事が出来ます。]

1はそのままなので良いでしょう。

2はBinderの一つの特徴となっています。
Linuxでは、ファイルをopenすると、カーネル内にそのファイルを表すファイルオブジェクトが出来て、
各ユーザープロセスにはこのファイルオブジェクトを表すテーブルが作られます。
この各テーブルに対応するファイルオブジェクトが入り、テーブルのインデックスがファイルディスクリプタと呼ばれる物になります。

//image[1_3_1][ファイルディスクリプタとファイルディスクリプタテーブル][scale=0.35]


binderドライバはプロセスAからプロセスBにファイルディスクリプタを送信している事に気づいたら、
プロセスAのファイルディスクリプタテーブルを見て、その参照先のファイルオブジェクトを探し出し、それをプロセスBのファイルディスクリプタテーブルに追加し、そのテーブルのインデックスに変換します。
これは双方のタスク構造体というプロセスを表すデータ構造を直接触れるデバイスドライバだからこそ出来る芸当です。

//image[1_3_2][ファイルディスクリプタを送る場合に起こる事]

3のサービスオブジェクトというのがBinderの中心となる物です。
技術的にはC++のBBinderのサブクラスという事になります。
BBinderのサブクラスをプロセスをまたいで送受信出来る、というのがBinderという物の中心的な機能です。
6章のActivityManagerServiceも、7.2.3で登場したApplicationThreadも、BBinderのサブクラスです。

サービスオブジェクトを送信して、相手のプロセスからこのサービスオブジェクトのメソッドを呼ぶ事が出来る、これがBinderです。

===[column] 分散オブジェクトいろいろ
世の中には様々な分散オブジェクトの仕組みがあります。私は私が関わった物以外はあまり知らないので、
ここで全ての一覧を提示する事は出来ませんが、幾つか名前を挙げてみましょう。

まず本書のコラムでもたまに言及される、分散オブジェクトの代表としてはCOMが挙げられます。
COMはWindowsで使われている分散オブジェクトのシステムで、トップクラスに使われている物の一つと言えます。
IEとOfficeがCOMから操れる、というのがこの技術がここまで使われた直接の理由だと思います。
このシステムではIDLの方言であるMIDLというインターフェース定義言語をコンパイルして、間のコードを自動生成していました。
COMは使う側から見るとシンプルなため、互換なシステムも良く作られました。
モバイルプラットフォームだとBREWやEMPといったシステムでは、インターフェースはCOMとなっています。

同じMicrosoftでも、より最近の.NETでは、WCFという分散オブジェクト技術があります。
これは分散オブジェクト技術を含んだより幅広い物で、オブジェクト呼び出し以外のweb APIのような呼び出しもオブジェクトの呼び出しのように呼び出す事が出来ます。
これはインターフェースから実行時にコードを自動生成します。
型情報がネイティブより多く動的コード生成が容易な仮想マシンではこのスタイルが主流となっていると思います。

Microsoft以外で一番有名な分散オブジェクトシステムと言えばCORBAでしょう。
昔はWindows以外で分散オブジェクトと言えばCORBAだった、と言っても過言では無いくらい、CORBA一色でした。
オープンな規格で言語非依存でその他様々な物に非依存な分散オブジェクトシステムです。
最近見かける所では、Linuxなどで良く使われるGnomeで採用されていました。

JavaのRMIなども典型的な分散オブジェクト技術です。またEJBなどはトランザクションなど多くの付加機能がついていますが、
これも分散オブジェクト技術の一つと言えます。HORBというJavaのCORBA実装（少し厳密な言い方では無いですが）も有名です。
Javaはその他にもDynamic Proxyを用いたより軽量な分散オブジェクトのシステムをたまに見かける気がしますが、
EJB以降は分散オブジェクト自体があまり流行ってない印象を受けます。

OS XなどにもNSConnectionという分散オブジェクトの仕組みがあります。
Macもかつては複合ドキュメントを推していた時代があったと思いますが、
最近ではあまり聞かなくなりました。

こうしてみると分かるように、だいたい分散オブジェクト自身は流行りが終わった技術、という印象うを受けます。
一方でシステムを構築するには何かしらこの手の技術が無いと不便でもあり、
Androidがいまさら独自に必要最低限の物を作った、というのは、長くこの業界に居た人間としてはなかなか感慨深いものがあり、
何がサポートされていないかを見ると「あぁ、分散オブジェクトというと複雑になりがちだけど、結局必要なのはこの辺だったんだなぁ」と10年越しに答えを見せてもらったような気持ちになります。

== 8.1.4 サービスを実行する側のスレッド

Binderはよそのプロセスのオブジェクトを通常のオブジェクトのように呼ぶ事が出来る、という仕組みなので、
普段は深くいろいろ考えずに、よそのプロセスのオブジェクトのメソッドを呼んでいる、と考えるだけで良いようになっています。

ですが、少し細かい事を考え出すと事はそう簡単ではありません。
まず、スレッドがどうなっているのか、という問題があります。
プロセスAがプロセスBのメソッドを呼んでいるとしましょう。
プロセスBのメソッドを実行する時にはプロセスBの中にこのメソッドを実行しているスレッドが無くてはいけません。

通常スレッドはプロセス内で作られます。外部から勝手にスレッドを作って走らせる、という事は、普通はしません。
ですから、プロセスBでメソッドを実行しているスレッドは、プロセスBが作ったものであるはずです。
そこで通常、プロセスBは、あらかじめBinderのメソッド実行専用のスレッドを作っておいて、
ずっと外部のプロセスからの呼び出しを待ち構えておきます。
この待ち構えているスレッドが、メソッドを実行するのです。
これはメインスレッドとは異なるスレッドです。

AndroidではJavaのプロセスが起動した時に、このスレッドを開始して待ち続けます。
詳細はzzzで扱います。
#@# TODO 

重要なポイントとしては、このBinder越しに呼び出された側のスレッドは、いつもGUIスレッドとは別のBinder処理用のスレッドだ、という事です。
他の章を読む時には、ここは意識しておく必要があります。


== 8.1.5 サービス実装とサービスプロキシ

Binderでサービスオブジェクトを送る事が出来る、という話をしました。

実際には送った先はプロキシオブジェクトという物に変換されています。
これはメソッド呼び出しを、そのメソッド呼び出しを表すメッセージに変換して、
サービス実装側のプロセスに送信する、というオブジェクトです。

//image[1_5_1][プロキシのメソッドを呼ぶとメッセージが送信される]

多くの場所ではその事を意識せずにただよそのプロセスのオブジェクトのメソッドを呼んでいる、
と思っておけば良いのですが、たまにその幻想が綻びていて、
実際はプロキシである、という事を意識しないといけない事があります。

そこで実装している側と、それを呼ぶプロキシ側に分かれている、という事は知っておく必要があります。
その現実が一番深刻に表れてしますのが次のIBinderです。


== 8.1.6 IBinderとXX.Stub.asInterfaceとXX.Stub.Proxy

アプリを開発していると、Binderという事を意識しないといけない事はほとんどないのですが、
たまに出てくるのがこのIBinderという奴です。ソースコードを調べていても出てくると思います。
同じ名前のC++のクラスもありますが、ここではJavaのIBinderの話です。

このIBinderとは何なのか？とソースコードを読んでもなんだか何もしてないように見える。
そしてそれがなんだかごちゃごちゃしたコードのXX.Stub.asInterface()とかの引数に渡されている。
なんだか良く分からないので、見様見真似でコピペで済ます、というのが良くあるIBinderを前にした光景に思います。

これを全部ちゃんと理解しようとすると本章の内容をちゃんと追っていく必要が出てきてしまいますが、
概念的な事はそこまで深く理解してなくても理解は出来るので、ここで簡単に説明しておきます（それでもやや難しいですが…）。

IBInderとは、サービスの実装とプロキシの両方を区別なく扱うためのクラスです。
イメージとしてはC言語のunionのようなもので、実態としてはサービス実装とサービスプロキシのどちらかが入っています。
このどちらかが入っているがどちらが入っているかが分からない、というのがコードを読みにくくしています。

IBinderはサービスのオブジェクトがそのまま入っている場合はただキャストすれば良い訳です。
少し読みにくいのがプロキシ側だった場合です。

プロキシのクラスは、通信用のオブジェクトをラップして作られています。
そしてIBinderはプロキシの時は通信用のオブジェクトが入っています。
だからプロキシオブジェクトを得るためには、この通信用オブジェクトをプロキシのクラスのコンストラクタに渡してやる必要があります。
このラップするプロキシオブジェクトのクラス名は「インターフェース名.Stub.Proxy」という名前にする事になっています。
例えばIHelloWorldというインターフェースなら、プロキシクラスはIHelloWorld.Stub.Proxyとなります。
おかしな名前ですが、これはJavaの言語の制約上から来ていて、間のStubに大した意味はありません。

ですから、IBinderがプロキシの場合には、これをコンストラクタに渡してプロキシクラスを作るのです。

//list[code2][IBinderからプロキシクラスを作る例]{
// もしこの引数のtokenがプロキシなら
void someMethod(IBinder token) {

    // プロキシクラスのコンストラクタに渡して、ラップするプロキシオブジェクトを作る
    IHelloWorld proxy = new IHelloWorld.Stub.Proxy(token);

    // 以下このproxyのメソッドを呼び出す
}
//}

この、

 1. サービス実装のオブジェクトならただキャストするだけ
 2. プロキシの通信用オブジェクトならラップしたオブジェクトを返す

の二つをやってくれるのがXX.Stub.asInterface()です。
IHelloWorldというインターフェースなら、IHelloWorld.Stub.asInterface(token)と呼ぶと、
とにかくこのIHelloWorldのインターフェースとして使えるオブジェクトが返ってくるので、
実装者はこのIBInderがどちらだったのか、という事を気にする必要は無い訳です。

まとめると以下のようになります。

 1. IBinderにはサービス実装のオブジェクトがそのまま入っている場合と、プロキシに使う通信オブジェクトが入っている場合がある
 2. 「インターフェース名.Stub.Proxy」という名前がプロキシクラスで、これは通信オブジェクトをラップして機能する
 3. 「インターフェース名.Stub.asInterface()」というメソッドがIBinderのそれぞれの場合を適切に処理してくれる

この三つが、Binderになるべく関わらないようにしている人でもたまに必要となる知識です。


== 8.1.6 Binderの基礎知識、まとめ

本節では、難しい話題を理解していなくともこれだけ知っていれば他の部分のソースを読む時に困らない、
という事をまとめてみました。

 1. Binderのカタログスペック的な特徴を知っている
 2. getSystemService()を使ったオブジェクトに対する操作はBinderを使っている事を知っている
 3. 何が送信できるか知っている
 4. サービスを実行しているスレッドがGUIスレッドで無い事を知知っている
 5. IBinderと「インターフェース名.Stub.Proxy」と「インターフェース名.Stub.asInterface()」を知っている

くらいを抑えておけば、内部の詳細に立ち入らなくても他の部分を理解する事は出来ると思います。
内部の詳細を理解せずにこれらの事を理解しようとすると、どうしてもぼやっとした部分が残ってしまうと思いますし、
たまにここでは扱ってない事もちょっとはあると思いますが、それはそういうものだ、と飲み込んでしまって、
詳細を学ぶ時間を他の事に充てるのも一つの選択でしょう。

ここより先は、もっと細かい事を知りたい、という人の為の内容となります。