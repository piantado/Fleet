
# for lang in SimpleEnglish MediumEnglish FancyEnglish Fibo AnBnCn AnBnC2n ABAnBn AB ABn An AnB2n AnBn Dyck AnBm AAAA AAA ABAnBn AnBmCmAn AnBmCnDm ABA ABB Count AnBkCn XX XXX XXI XXR XXRI XY Man Reber Saffran BerwickPilato Gomez2 Gomez6 Gomez12 NewportAslin MorganNewport MorganMeierNewport HudsonKamNewport100 HudsonKamNewport75 HudsonKamNewport60 HudsonKamNewport45 ReederNewportAslin GoldenMean Even AnBnCnDn A2en ABnen AnCBn AnABn AnABAn ABnABAn

makeLang() {
	echo "Making language " $1

	for d in 1 2 5 10 50 100 500 1000 5000 10000 50000 100000; do
		python MakeData.py $d $1 > data/$1-$d.txt
	done
	
	python MakeData.py 1000000 $1 > data/$1.txt
}

export -f makeLang

#languages=(SimpleEnglish MediumEnglish FancyEnglish AnBnCn AnBnC2n ABAnBn AB ABn An AnB2n AnBn Dyck AnBm AAAA AAA ABAnBn AnBmCmAn AnBmCnDm ABA ABB Count AnBkCn XX XXX XXI XXR XXRI XY Man Reber Saffran BerwickPilato Gomez2 Gomez6 Gomez12 NewportAslin MorganNewport MorganMeierNewport HudsonKamNewport100 HudsonKamNewport75 HudsonKamNewport60 HudsonKamNewport45 ReederNewportAslin GoldenMean Even AnBnCnDn A2en ABnen AnCBn Bach2 Bach3 AnBmCn AnBmCm AnBmCnpm AnBmCnm AnBk ABaaaAB aABb Elman Braine66 PullumR ApBAp AsBAsp ApBApp CountA2 CountAEven Fibo AnBnCnDnEn AnBmAnBm AnBmAnBmCCC WeW An2 ChineseNumeral AnBmA2n Unequal) 

languages=(AnBmAnBm) 

# Run in parallel on all languages
parallel makeLang ::: "${languages[@]}"
