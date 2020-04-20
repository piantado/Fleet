library(latex2exp)

logsumexp <- function(x) { log(sum(exp(x-max(x))))+max(x)}
# paste(\"\", \"\", \"{\", paste(\"\", \"A\", \",\", \"B\", \"\"), \"}\", \"\", phantom()^{\n    paste(phantom() + phantom())\n}, \"\", \"\")
label <- list("An"=paste(TeX("A^n")),
              "aABb"="paste(\"\", \"A\", \"\", \"{\", paste(\"\", \"A\", \",\", \"B\", \"\"), \"}\", \"\", phantom()^{\n    paste(phantom() + phantom())\n}, \"\", \"B\", \"\")",
              #paste(TeX("$A \\left{ A,B \\right}^+ B$")),
              "AB"="paste(\"\", \"\", \"{\", paste(\"\", \"A\", \",\", \"B\", \"\"), \"}\", \"\", phantom()^{\n    paste(phantom() + phantom())\n}, \"\", \"\")", #,paste(TeX("$\\left{ A,B \\right}^+$")),
              "ABaaaAB"="paste(\"\", \"\", \"{\", paste(\"\", \"A\", \",\", \"B\", \"\"), \"}\", \"\", phantom()^{\n    paste(phantom() + phantom())\n}, \"\", \"AAA\", \"\", \"{\", paste(\"\", \"A\", \",\", \"B\", \"\"), \"}\", \"\", phantom()^{\n    paste(phantom() + phantom())\n}, \"\", \"\", \"\")",
              #paste(TeX("$\\left{ A,B \\right}^+ AAA \\left{ A,B \\right}^+ $")),
              "AnABn"=paste(TeX("A^n(AB)^n")),
              "AnABAn"=paste(TeX("A^n(ABA)^n")),
              "ABnABAn"=paste(TeX("(AB)^n(ABA)^n")),
              "ABn"=paste(TeX("(AB)^n")),
              "AnBn"=paste(TeX("A^{n}B^{n}")),
              "AnB2n"=paste(TeX("A^{n}B^{2n}")),
              "AnBm"=paste(TeX("A^{n}B^{n+m}")),
              "Fibo"=paste(TeX("Fibonacci")),
              "AnBnCn"=paste(TeX("A^{n}B^{n}C^{n}")),
              "AnBnC2n"=paste(TeX("A^{n}B^{n}C^{2n}")),
              "AnBmCn"=paste(TeX("A^{n}B^{m}C^{n}")),
              "AnBk"=paste(TeX("A^{n}B^{k}, k>m")),
              "AnBmCm"=paste(TeX("A^{n}B^{m}C^{m}")),
              "AnBmCnpm"=paste(TeX("A^{n}B^{m}C^{n+m}")),
              "AnBmCnm"=paste(TeX("A^{n}B^{m}C^{nm}")),
              "AnBmCmAn"=paste(TeX("A^{n}B^{m}C^{m}A^{n}")),
              "AnBmCnDm"=paste(TeX("A^{n}B^{m}C^{n}D^{m}")),
              "AnBnCnDn"=paste(TeX("A^{n}B^{n}C^{n}D^{n}")),
              "ABAnBn"="paste(\"\", \"\", \"{\", paste(\"\", \"A\", \",\", \"B\", \"\"), \"}\", \"\", phantom()^{\n    paste(phantom() + phantom())\n}, \"\", \"A\", phantom()^{\n    paste(\"n\")\n}, \"B\", phantom()^{\n    paste(\"n\")\n}, \"\")",
              #paste(TeX("$\\left{ A,B \\right}^+ A^{n}B^{n}$")),              
              "XXR"=paste(TeX("XX^R")),
              "XXI"=paste(TeX("XX^I")),
              "A2en"=paste(TeX("A^{2^n}")),
              "ABnen"=paste(TeX("((AB)^n)^n")),
              "AnCBn"=paste(TeX("A^n C B^n")),
              "An2"=paste(TeX("a^{n^2}")),
              "AnBmAnBm"=paste(TeX("a^n b^m a^n b^m")),
              "AnBmA2n"=paste(TeX("a^n b^m a^{2n}")),
              "AnBmAnBmCCC"=paste(TeX("a^n b^m a^n b^m ccc")),
              "PullumR"=paste(TeX("a (c^x d c^y d c^z)^w b")),
              "ApBAp"=paste(TeX("a^+ b a^+")),
              "AsBAsp"=paste(TeX("a^* (b a^*)^+")),
              "ApBApp"=paste(TeX("a^* (b a^+)^+")),
              "CountAEven"=paste(TeX("\lbrace w \in \lbrace a,b \rbrace^+ : count(a,w) \text{ is even} \brace")),              
              "WeW"=paste(TeX("\lbrace w^{|w|} : w \in \lbrace a,b \rbrace^* \rbrace")),
              "MorganMeierNewport"="MMN",
              "ReederNewportAslin"="RNA",
              "HudsonKamNewport60"="HKN60"
              )
D <- NULL
# for(language in c("Gomez2", "Gomez6", "Gomez12" )) {
# for(language in c("HudsonKamNewport45", "HudsonKamNewport60", "HudsonKamNewport75", "HudsonKamNewport100" )) {
for(language in c("An", "ABn", "AnBn", "AB", "ABAnBn", "AnB2n", "AnBm", "AnBmCn", "XXR", "AAA", "AAAA", "Count", "AnBnCn", "AnBnC2n", "Dyck", "XX", "XXX",  "XXI", "XY", "AnBmCmAn", "AnBmCnDm", "GoldenMean", "Even", "AnBnCnDn", "A2en", "ABnen", "AnCBn", "AnABn", "AnABAn", "ABnABAn", "Bach2", "Bach3", "AnBm", "AnBmCn", "AnBmCm", "AnBmCnpm", "AnBmCnm", "AnBk", "ABaaaAB", "aABb", "Elman", "Braine66", "PullumR", "ApBAp", "AsBAsp", "ApBApp", "CountA2", "CountAEven", "Fibo", "AnBnCnDnEn", "AnBmAnBmCCC", "WeW", "An2", "AnBmAnBm", "ChineseNumeral", "AnBmA2n", "Unequal", "Milne")) {
# for(language in c("Reber", "Elman", "Saffran",  "ABA", "ABB", "NewportAslin", "MorganNewport", "MorganMeierNewport", "Man", "BerwickPilato", "ReederNewportAslin", "HudsonKamNewport60", "Gomez2", "Gomez6", "Gomez12" )) {
# for(language in c("SimpleEnglish", "MediumEnglish", "FancyEnglish" )) {
# for(language in c("MediumEnglish")) {
    q <- NULL
    for(nf in c(1,2,3,4,5,6,7,8)) {

            f <- paste("./out-2020Feb23/", language, "-", nf, ".out", sep="")
#               f <- paste("/home/piantado/Desktop/Science/out/", language, "-", nf, ".out", sep="")
             
            r <- try(read.table(f, quote="\""))
            if(class(r)=='try-error') {
                print(c("Error reading ", f))
                next;
            }
                
            print(f)
            names(r) <- c("ndata", "born", "posterior", "prior", "likelihood", "parseable", "precision", "recall", "h")
            
            q <- rbind(q, r)
    }
    
    if(!is.null(q)) {
        for(Q in split(q, q$ndata)) {
            
            Q$z <- logsumexp(Q$posterior)
            
            l <- ifelse(language %in% names(label), label[[language]], language)
            D <- rbind(D, data.frame(language=l, ndata=Q$ndata[1], file=f, Measure="precision", value=sum( exp(Q$posterior-Q$z) * Q$precision)))
            D <- rbind(D, data.frame(language=l, ndata=Q$ndata[1], file=f, Measure="recall",    value=sum( exp(Q$posterior-Q$z) * Q$recall)))
            D <- rbind(D, data.frame(language=l, ndata=Q$ndata[1], file=f, Measure="F",         value=sum( exp(Q$posterior-Q$z) * 2.0/(1.0/Q$recall + 1.0/Q$precision))))
        }
    }
}

## ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`

library(ggplot2)
require(scales)

plt <- ggplot(D, aes(x=ndata, y=value, group=Measure, color=Measure)) + 
	  geom_line(size=0.70, position=position_dodge(.1)) +  # a little dodging here to show overlap
	  theme_bw() +
	  xlab("Amount of data") + ylab("") +
 	  scale_x_log10(breaks=c(1,10,100,1000,10000,100000), labels=c("1", "10", "100", expression(10^3), expression(10^4), expression(10^5)))  +
      #scale_x_log10(breaks = trans_breaks("log10", function(x) 10^x),
      #        labels = trans_format("log10", math_format(10^.x))) +
	  ylim(-1e-9,1.01) +
 	  facet_wrap( ~ language, labeller=label_parsed) + 
	  theme(strip.background = element_rect(fill="white"))
plt
ggsave("SimpleLanguages.pdf", plt, width=12, height=9)

## For plotting English:
# plt <- ggplot(D, aes(x=ndata, y=value, group=Measure, color=Measure)) + 
# 	  geom_line(size=1.2, position=position_dodge(.1)) +  # a little dodging here to show overlap
# 	  theme_bw() +
# 	  xlab("Amount of data") + ylab("") +
#  	  scale_x_log10(breaks=c(1,100,10000))  +
# 	  ylim(-1e-9,1.01) +
# 	  theme(strip.background = element_rect(fill="white"))
# plt
# ggsave("English.pdf", plt)

# ggsave("SimpleLanguages.pdf", plt)
# ggsave("ArtificialLanguages.pdf", plt)
# ggsave("EnglishLanguages.pdf", plt)
