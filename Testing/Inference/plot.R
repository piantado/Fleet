library(ggplot2)

d <- read.table("o.txt", header=F)
names(d) <- c("repetition", "runtime", "method", "restart", "data", "chains", "inner.times", "posterior", "prior", "likelihood", "hypothesis")

d$chains <- as.factor(d$chains)
d$restart <- as.factor(d$restart)

plt <- ggplot(d, aes(x=runtime, y=posterior, color=chains, group=chains)) + 
    stat_summary(fun=mean, geom="line") + 
    scale_x_log10() +
    facet_grid(data~method, scales="free") + 
    theme_bw()
plt


# d$restart.innertime <- paste0(d$restart, ":", d$inner.times) # aggregate variable to facet by
# plt <- ggplot(subset(d, method=="parallel-tempering"), aes(x=runtime, y=posterior, color=chains)) + 
#     stat_summary(fun=mean, geom="line") + 
#     scale_x_log10() +
#     facet_grid(data~restart, scales="free") + 
#     theme_bw()
# plt
