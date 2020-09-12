library(ggplot2)

d <- read.table("o.txt",header=F)
names(d) <- c("samples", "variable", "value")

plt <- ggplot(d, aes(x=samples, y=value)) + 
        geom_line() + 
        facet_wrap(~variable, scales="free")
