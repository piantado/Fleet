#!/bin/bash


# Run easy ones:: 10m on factors 1,2,3,4
#30m, 4h 24h
factors="2 4"


{
	for nf in $factors; do
                ARGS=( --time=10m --thin=0 --mcmc=0  --threads=6 --top=1000 --restart=100000 --nfactors=$nf )

                for lang in AAA AB ABn An AnB2n AnBn AnBm AAAA AnBnCn AnBkCn AnBnC2n XXR XX XXX GoldenMean Even AnBnCnDn AnCBn
                do
                        /usr/bin/time --output=out/$lang-$nf.time --verbose ./main "${ARGS[@]}" --input=data/$lang --alphabet=abcd >out/$lang-$nf.out 2>out/$lang-$nf.err 
                done
                
                for lang in ABA ABB
                do
                        /usr/bin/time --output=out/$lang-$nf.time --verbose ./main "${ARGS[@]}" --input=data/$lang --alphabet=gGtTnNlL >out/$lang-$nf.out 2>out/$lang-$nf.err 
                done

	done
} & 


{

	for nf in $factors; do
        
                ARGS=( --time=8h --thin=0 --mcmc=0  --threads=6 --top=1000 --restart=100000 --nfactors=$nf )

                for lang in XXI XXRI XY ABAnBn A2en ABnen
                do
                        /usr/bin/time --output=out/$lang-$nf.time --verbose ./main "${ARGS[@]}" --input=data/$lang --alphabet=ab >out/$lang-$nf.out 2>out/$lang-$nf.err 
                done
    done


	for nf in $factors; do
        
                ARGS=( --time=8h --thin=0 --mcmc=0  --threads=6 --top=1000 --restart=100000 --nfactors=$nf )

                for lang in AnBmCmAn AnBmCnDm
                do
                        /usr/bin/time --output=out/$lang-$nf.time --verbose ./main "${ARGS[@]}" --input=data/$lang --alphabet=abcd >out/$lang-$nf.out 2>out/$lang-$nf.err 
                done
    done

} &


{
    for nf in $factors; do
        
        ARGS=( --time=4h --thin=0 --mcmc=0  --threads=6 --top=1000 --restart=100000 --nfactors=$nf )
        
        /usr/bin/time --output=out/SimpleEnglish-$nf.time --verbose ./main "${ARGS[@]}" --input=data/SimpleEnglish   --alphabet=dnavt >out/SimpleEnglish-$nf.out 2>out/SimpleEnglish-$nf.err 
        /usr/bin/time --output=out/MediumEnglish-$nf.time --verbose ./main "${ARGS[@]}" --input=data/MediumEnglish   --alphabet=dnavtp >out/MediumEnglish-$nf.out 2>out/MediumEnglish-$nf.err 
        
        /usr/bin/time --output=out/ReederNewportAslin-$nf.time --verbose ./main "${ARGS[@]}" --input=data/ReederNewportAslin        --alphabet=aAsbBnxXcqQrR >out/ReederNewportAslin-$nf.out 2>out/ReederNewportAslin-$nf.err 

        for lang in Gomez2 Gomez6 Gomez12
        do
                /usr/bin/time --output=out/$lang-$nf.time --verbose ./main "${ARGS[@]}" --input=data/$lang --alphabet=abcde1234567890wxyz >out/$lang-$nf.out 2>out/$lang-$nf.err 
        done
        
        for lang in HudsonKamNewport60 #HudsonKamNewport100 HudsonKamNewport75 HudsonKamNewport45 
        do
                /usr/bin/time --output=out/$lang-$nf.time --verbose ./main "${ARGS[@]}" --input=data/$lang --alphabet="!vVnd" >out/$lang-$nf.out 2>out/$lang-$nf.err 
        done

    done
} & 




{
    for nf in $factors; do
        
        ARGS=( --time=8h --thin=0 --mcmc=0  --threads=6 --top=1000 --restart=100000 --nfactors=$nf )
    
        /usr/bin/time --output=out/Dyck-$nf.time --verbose ./main "${ARGS[@]}" --input=data/Dyck        --alphabet="()" >out/Dyck-$nf.out 2>out/Dyck-$nf.err 

        /usr/bin/time --output=out/Saffran-$nf.time --verbose ./main "${ARGS[@]}" --input=data/Saffran      --alphabet=tprglbBdkPDT >out/Saffran-$nf.out 2>out/Saffran-$nf.err 

        /usr/bin/time --output=out/NewportAslin-$nf.time --verbose ./main "${ARGS[@]}" --input=data/NewportAslin    --alphabet=btgdprkuli1234 >out/NewportAslin-$nf.out 2>out/NewportAslin-$nf.err 
        /usr/bin/time --output=out/MorganNewport-$nf.time --verbose ./main "${ARGS[@]}" --input=data/MorganNewport   --alphabet=ACDEF >out/MorganNewport-$nf.out 2>out/MorganNewport-$nf.err 
        /usr/bin/time --output=out/MorganMeierNewport-$nf.time --verbose ./main "${ARGS[@]}" --input=data/MorganMeierNewport --alphabet=ACDEFouai >out/MorganMeierNewport-$nf.out 2>out/MorganMeierNewport-$nf.err
        /usr/bin/time --output=out/Man-$nf.time --verbose ./main "${ARGS[@]}" --input=data/Man        --alphabet=man >out/Man-$nf.out 2>out/Man-$nf.err 
        
    done
} & 



{
    for nf in $factors; do

            ARGS=( --time=24h --thin=0 --mcmc=0  --threads=6 --top=1000 --restart=100000 --nfactors=$nf )
            
            /usr/bin/time --output=out/FancyEnglish-$nf.time --verbose ./main "${ARGS[@]}" --input=data/FancyEnglish   --alphabet=dnavtpih >out/FancyEnglish-$nf.out 2>out/FancyEnglish-$nf.err 
            /usr/bin/time --output=out/Reber-$nf.time --verbose ./main "${ARGS[@]}" --input=data/Reber        --alphabet=PSTVX >out/Reber-$nf.out 2>out/Reber-$nf.err 
            /usr/bin/time --output=out/BewickPilato-$nf.time --verbose ./main "${ARGS[@]}" --input=data/BerwickPilato   --alphabet=JgGdDeiWhHNvVmMjbBEow >out/BerwickPilato-$nf.out 2>out/BerwickPilato-$nf.err 
    done
} & 

