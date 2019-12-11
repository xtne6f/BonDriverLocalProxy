BonDriverLocalProxy

■概要
他のBonDriverへパイプ越しにプロキシ接続するツールです。ローカル専用の
BonDriverProxyのようなものです。

■使い方
BonDriverを利用するアプリのあるフォルダと同じ階層、またはシステムドライブ(普通は
Cドライブ)に"BonDriverProxy"フォルダ(名前に注意)を作成し、プロキシ接続させたい
BonDriverとBonDriverLocalProxy.exeを置いてください。
プロキシ接続したいアプリにBonDriver_Proxy.dllを以下のようにリネームして置いてく
ださい。
  BonDriver_Proxy?_{接続するBonDriverの"BonDriver_*.dll"の*部分}.dll

?部分(0～1文字の大小区別なし英数字)でチャンネル変更の優先度を指定します。?部分が
同じものは同時接続できません。基本的に無→0→9→A→Zで後者ほど高優先度ですが、以
下のようにグループになります。
  無  優先度1
  0-4 優先度2の後続優先グループ
  5-9 優先度3の先行優先グループ
  A-G 優先度4の後続優先グループ
  H-N 優先度5の先行優先グループ
  O-T 優先度6の後続優先グループ
  U-Z 優先度7の先行優先グループ

"BonDriver_hoge.dll"をプロキシ接続させたい場合、BonDriver_Proxy.dllは例えば以下
のようにリネームすると、下にいくほど優先度が高くなります。
  低 BonDriver_Proxy_hoge.dll
  ↓ BonDriver_Proxy0_hoge.dll
  ↓ BonDriver_Proxy5_hoge.dll
  ↓ BonDriver_ProxyA_hoge.dll (Bより後に接続すればチャンネル変更可能)
  同 BonDriver_ProxyB_hoge.dll (Aより後に接続すればチャンネル変更可能)
  高 BonDriver_hoge.dll (プロキシ元と同名のものは最高優先度)

対等指定はできません。優先度の高いアプリが接続しているとき、これ以外のアプリはチ
ャンネル変更できません。設定ファイルはありません。

■ライセンス
MITとします。IBonDriver*.hの追記部分はパブリックドメインとします。

■ソース
https://github.com/xtne6f/BonDriverLocalProxy

■謝辞
拡張ツール中の人のIBonDriver*.hをインクルードしています。
利便性のためBonDriver_Proxy.dllという名前を借用していますが、本家の
BonDriverProxy( https://github.com/u-n-k-n-o-w-n/BonDriverProxy )とは直接関係あ
りません。ただし、作成にあたってとても参考になりました。
