library(ggplot2)

d <- read.table("o.txt", header=F)
names(d) <- c("repetition", "runtime", "method", "restart", "data", "chains", "posterior", "prior", "likelihood", "hypothesis")

d$chains <- as.factor(d$chains)
d$restart <- as.factor(d$restart)

plt <- ggplot(subset(d, restart==0), aes(x=runtime, y=posterior, color=chains, group=chains)) + 
    stat_summary(fun=mean, geom="line") + 
    scale_x_log10() +
    facet_grid(data~method, scales="free")
