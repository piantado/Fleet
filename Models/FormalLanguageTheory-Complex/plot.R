library(latex2exp)

logsumexp <- function(x) { log(sum(exp(x-max(x))))+max(x)}


rp <- read.csv("AllParameters.csv", header=F)
names(rp) <- c("language", "alphabet", "blah", "runtime", "type", "latex")

label <- setNames(as.list(as.character(rp$latex)), rp$language)  # label[[language]] = latex


              
D <- NULL
# for(language in c("Gomez2", "Gomez6", "Gomez12" )) {
# for(language in c("HudsonKamNewport45", "HudsonKamNewport60", "HudsonKamNewport75", "HudsonKamNewport100" )) {
# for(language in c("An", "AB", "ABn", "AAA", "AAAA", "AnBm", "GoldenMean", "Even", "ApBAp", "AsBAsp", "ApBApp", "ABaaaAB", "CountA2", "CountAEven", "PullumR", "aABb", "AnBn", "Dyck", "AnB2n", "AnCBn", "AnABn", "AnABAn", "ABnABAn", "AnBmCn", "AnBmA2n", "AnBnC2n", "ABAnBn", "AnBmCm", "AnBmCnpm", "AnBmCnm", "AnBk", "AnBmCmAn", "AnB2nC3n", "AnBnp1Cnp2", "AnUBn", "AnUAnBn", "ABnUBAn", "XX", "XXX", "XY", "XXR", "XXI", "XXRI", "Unequal", "Bach2", "Bach3", "WeW", "An2", "AnBmCnDm", "AnBmAnBm", "AnBmAnBmCCC", "AnBnCn", "AnBnCnDn", "AnBnCnDnEn", "A2en", "ABnen", "Count", "ChineseNumeral", "Fibo")) {
for(language in c("NewportAslin", "MorganNewport", "MorganMeierNewport", "Braine66", "ABA", "ABB", "HudsonKamNewport60", "Gomez2", "Gomez6", "Saffran", "Milne", "Elman", "Man", "Gomez12", "ReederNewportAslin", "Reber", "BerwickPilato")) {
# for(language in c("English")) {
    q <- NULL
    for(nf in c(1,2,3,4,5,6,7,8)) {
#       for(nf in c(1)) {

            f <- paste("./out/", language, "-", nf, ".out", sep="")
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
            
            D <- rbind(D, data.frame(language=language, ndata=Q$ndata[1], file=f, Measure="precision", value=sum( exp(Q$posterior-Q$z) * Q$precision)))
            D <- rbind(D, data.frame(language=language, ndata=Q$ndata[1], file=f, Measure="recall",    value=sum( exp(Q$posterior-Q$z) * Q$recall)))
            D <- rbind(D, data.frame(language=language, ndata=Q$ndata[1], file=f, Measure="F",         value=sum( exp(Q$posterior-Q$z) * 2.0/(1.0/Q$recall + 1.0/Q$precision))))
        }
    }
}

## ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`
## Set the levels of D$language to match rp


# Convert languages to TeX
# (Not done particularly efficiently since it will run multiple times for each language
D$languageTeX <- NA
for(r in 1:nrow(D)) {
    language <- as.character(D$language[r])
    D$languageTeX[r] <- TeX( ifelse(language %in% names(label), label[[language]], language) )
}


    
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
 	  facet_wrap( ~ paste(languageTeX), labeller=label_parsed) + 
	  theme(strip.background = element_rect(fill="white")) +
	  theme(legend.position = c(1, 0), legend.justification = c(1.5, 0))
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

## For plotting the artificial languages
# plt <- ggplot(D, aes(x=ndata, y=value, group=Measure, color=Measure)) + 
# 	  geom_line(size=0.70, position=position_dodge(.1)) +  # a little dodging here to show overlap
# 	  theme_bw() +
# 	  xlab("Amount of data") + ylab("") +
#  	  scale_x_log10(breaks=c(1,10,100,1000,10000,100000), labels=c("1", "10", "100", expression(10^3), expression(10^4), expression(10^5)))  +
#       #scale_x_log10(breaks = trans_breaks("log10", function(x) 10^x),
#       #        labels = trans_format("log10", math_format(10^.x))) +
# 	  ylim(-1e-9,1.01) +
#  	  facet_wrap( ~ language, labeller=label_parsed, nrow=2) + 
# 	  theme(strip.background = element_rect(fill="white"))
# plt
# ggsave("ArtificialLanguages.pdf", plt, height=4, width=14)


# ggsave("SimpleLanguages.pdf", plt)
# 
# ggsave("EnglishLanguages.pdf", plt)
