library(ggplot2)

d <- read.table("o.txt", header=F)
names(d) <- c("repetition", "runtime", "method", "restart", "data", "chains", "inner.times", "explore", "partition.depth", "posterior", "prior", "likelihood", "hypothesis")

d <- subset(d, restart==0)

d$chains <- as.factor(d$chains)
d$restart <- as.factor(d$restart)

d$method.plot <- as.character(d$method)
d$method.plot <- ifelse(d$method=="partition-mcmc",     paste0("partition-mcmc ", d$partition.depth),             d$method.plot)
d$method.plot <- ifelse(d$method=="mcmc-within-mcts",   paste0("mcmc-within-mcts ", d$inner.times, " ", d$explore),    d$method.plot)
d$method.plot <- ifelse(d$method=="prior-sample-mcts",  paste0("prior-sample-mcts ", d$inner.times, " ", d$explore),   d$method.plot)
# d$method.plot <- ifelse(d$method=="parallel-tempering", paste0("parallel-tempering ", d$chains),                  d$method.plot)

plt <- ggplot(subset(d, restart=0), aes(x=runtime, y=posterior, color=chains, group=chains)) + 
    stat_summary(fun=mean, geom="line") + 
    scale_x_log10() +
    facet_grid(data ~ method.plot, scales="free") + 
    theme_bw()
plt

q <- subset(d, data=="max" & restart==0)
plt2 <- ggplot(q, aes(x=runtime, y=posterior, color=chains, group=chains)) + 
    stat_summary(fun=mean, geom="line") + 
    scale_x_log10() +
    facet_wrap(~ method.plot) + 
    theme_bw()
plt2



# d$restart.innertime <- paste0(d$restart, ":", d$inner.times) # aggregate variable to facet by
# plt <- ggplot(subset(d, method=="parallel-tempering"), aes(x=runtime, y=posterior, color=chains)) + 
#     stat_summary(fun=mean, geom="line") + 
#     scale_x_log10() +
#     facet_grid(data~restart, scales="free") + 
#     theme_bw()
# plt
