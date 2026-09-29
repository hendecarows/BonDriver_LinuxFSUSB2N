# BonDriver_LinuxFSUSB2N

Linux版[EDCB][link_edcb]で使用することを目的に、◆WPjeGg6tSA氏が作成した[recfsusb2n][link_recfsusb2n]をLinux版BonDriver化したものです。

BonDriver化にあたり、[recfsusb2n][link_recfsusb2n]とnns779氏の[BonDriver_LinuxPTX][link_bonptx]のソースファイルを修正して使用しています。

## インストール

### ビルド

git, cmakeを含むビルドツールをインストールした状態でビルドします。

```sh
git clone https://github.com/hendecarows/BonDriver_LinuxFSUSB2N.git
cd BonDriver_LinuxFSUSB2N
mkdir build
cd build
cmake ..
make
```

### インストール

必要に応じてユーザーをvideoグループに追加し、再ログインします。

```sh
sudo adduser $USER video
```

udevルールを反映しデバイスを再接続します。

```sh
sudo cp ../99-fsusb2n.rules /etc/udev/rules.d
sudo udevadm control --reload
sudo udevadm trigger
```

BonDriver_LinuxFSUSB2N.so,iniを`/usr/local/lib/edcb`にコピーします。

```sh
sudo cp BonDriver_LinuxFSUSB2N.so /usr/local/lib/edcb
sudo cp ../BonDriver_LinuxFSUSB2N.ini /usr/local/lib/edcb
```

EpgDataCap_Bonでチャンネルスキャンします。

```sh
EpgDataCap_Bon -d BonDriver_LinuxFSUSB2N.so -chscan
```

## ライセンス

`FSUSB2N/`内が[recfsusb2n][link_recfsusb2n]由来のコードです。[recfsusb2n][link_recfsusb2n]
由来のコードに関するライセンスは[リンク先][link_readme]を確認して下さい。それ以外はソースファイルに記述されたライセンス
（基本的には[MIT][link_mit]）に従うものとします。

[link_edcb]: https://github.com/xtne6f/EDCB
[link_recfsusb2n]: https://ktvwiki.22web.org/index.php?Download#pcf424aa
[link_bonptx]: https://github.com/nns779/BonDriver_LinuxPTX
[link_readme]: https://ktvwiki.22web.org/index.php?plugin=attach&refer=Download&openfile=recfsusb2n_for_linux.htm&i=1
[link_mit]: LICENSE
