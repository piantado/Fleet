library(latex2exp)

logsumexp <- function(x) { log(sum(exp(x-max(x))))+max(x)}

label <- list("An"=paste(TeX("A^n")),
              "ABn"=paste(TeX("(AB)^n")),
              "AnBn"=paste(TeX("A^{n}B^{n}")),
              "AnB2n"=paste(TeX("A^{n}B^{2n}")),
              "AnBm"=paste(TeX("A^{n}B^{n+m}")),
              "Fibo"=paste(TeX("Fibonacci")),
              "AnBnCn"=paste(TeX("A^{n}B^{n}C^{n}")),
              "AnBnC2n"=paste(TeX("A^{n}B^{n}C^{2n}")),
              "AnBmCn"=paste(TeX("A^{n}B^{m}C^{n}")),
              "AnBmCmAn"=paste(TeX("A^{n}B^{m}C^{m}A^{n}")),
              "AnBmCnDm"=paste(TeX("A^{n}B^{m}C^{n}D^{m}")),
              "AnBnCnDn"=paste(TeX("A^{n}B^{n}C^{n}D^{n}")),
              "ABAnBn"=paste(TeX("\\{A,B\\}+A^{n}B^{n}")),              
              "XXR"=paste(TeX("XX^R")),
              "XXI"=paste(TeX("XX^I")),
              "A2en"=paste(TeX("A^{2^n}")),
              "ABnen"=paste(TeX("((AB)^n)^n")),
              "AnCBn"=paste(TeX("A^n C B^n")),
              "MorganMeierNewport"="MMN",
              "ReederNewportAslin"="RNA"
              )
D <- NULL
# for(language in c("Gomez2", "Gomez6", "Gomez12" )) {
# for(language in c("HudsonKamNewport45", "HudsonKamNewport60", "HudsonKamNewport75", "HudsonKamNewport100" )) {
for(language in c("An", "ABn", "AnBn", "AB", "ABAnBn", "AnB2n", "AnBm", "AnBmCn", "XXR", "AAA", "AAAA", "Count", "AnBnCn", "AnBnC2n", "Dyck", "XX", "XXX",  "XXI", "XY", "AnBmCmAn", "AnBmCnDm", "ABA", "ABB", "GoldenMean", "Even", "AnBnCnDn", "A2en", "ABnen", "AnCBn", "AnABn", "AnABAn", "ABnABAn", "Bach2", "Bach3", "AnBm", "AnBmCn", "AnBmCm", "AnBmCnpm", "AnBmCnm", "AnBk", "ABaaaAB", "aABb")) {
# for(language in c("Reber", "Saffran", "NewportAslin", "MorganNewport", "MorganMeierNewport", "Man", "BerwickPilato", "ReederNewportAslin", "HudsonKamNewport60", "Gomez2", "Gomez6", "Gomez12" )) {
# for(language in c("SimpleEnglish", "MediumEnglish", "FancyEnglish" )) {

    q <- NULL
    for(nf in c(1,2,3,4)) {

            f <- paste("out/", language, "-", nf, ".out", sep="")
                
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
            D <- rbind(D, data.frame(language=l, ndata=Q$ndata[1], file=f, measure="precision", value=sum( exp(Q$posterior-Q$z) * Q$precision)))
            D <- rbind(D, data.frame(language=l, ndata=Q$ndata[1], file=f, measure="recall",    value=sum( exp(Q$posterior-Q$z) * Q$recall)))
            D <- rbind(D, data.frame(language=l, ndata=Q$ndata[1], file=f, measure="F",         value=sum( exp(Q$posterior-Q$z) * 2.0/(1.0/Q$recall + 1.0/Q$precision))))
        }
    }
}

## ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~`

library(ggplot2)

plt <- ggplot(D, aes(x=ndata, y=value, group=measure, color=measure)) + 
	  geom_line(position=position_dodge(.1)) +  # a little dodging here to show overlap
	  theme_bw() +
	  xlab("Amount of data") + ylab("") +
 	  scale_x_log10(breaks=c(1,100,10000))  +
	  ylim(-1e-9,1.01) +
	  facet_wrap( ~ language, labeller=label_parsed) + 
	  theme(strip.background = element_rect(fill="white"))
plt

# ggsave("ArtificialLanguages.pdf", plt)
# ggsave("SimpleLanguages.pdf", plt)
# ggsave("EnglishLanguages.pdf", plt)
