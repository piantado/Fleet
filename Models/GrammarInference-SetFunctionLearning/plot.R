library(ggplot2)

d <- read.table("output/o.txt",header=F)
names(d) <- c("samples", "nt", "variable", "value")
d$nt <- as.factor(d$nt)

plt <- ggplot(d, aes(x=samples, y=value, col=nt)) + 
        geom_line() + 
        facet_wrap(nt~variable, scales="free") + 
        theme_bw()
