library(ggplot2)

d <- read.table("o.txt", header=F)
names(d) <- c("rp", "best.posterior", "h")
d$rp <- as.factor(d$rp)

plt <- ggplot(d, aes(x=rp, y=best.posterior)) +
	geom_boxplot()
	
