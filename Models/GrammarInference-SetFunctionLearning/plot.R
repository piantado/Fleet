library(ggplot2)

d <- read.table("output/parameters.txt",header=F)
names(d) <- c("samples", "nt", "variable", "value")
d$nt <- as.factor(d$nt)

plt <- ggplot(d, aes(x=samples, y=value, col=nt)) + 
        geom_line() + 
        facet_wrap(nt~variable, scales="free") + 
        theme_bw()

plt2 <- ggplot(d, aes(x=value,  fill=nt)) +
        geom_histogram() +
        facet_wrap(nt~variable, scales="free") + 
        theme_bw()
plt2
