library(ggplot2)

d <- read.table("output/heldout-best-predictions.txt", header=F)
names(d) <- c("conceptlist", "item", "decay.position", "correct", "nyes", "nno", "alpha", "model.ptrue")
d$human.ptrue <- d$nyes / (d$nyes+d$nno)
d$n.correct   <- ifelse(d$correct, d$nyes, d$nno)
d$n.incorrect <- ifelse(d$correct, d$nno,  d$nyes)

d$human.pcorrect <- ifelse(d$correct, d$nyes, d$nno) / (d$nyes+d$nno)
d$model.pcorrect <- ifelse(d$correct, 
						   d$alpha*d$model.ptrue+(1-d$alpha)*0.5, 
						   d$alpha*(1-d$model.ptrue)+(1-d$alpha)*0.5)

print(c("Best heldout.lp:", sum(log(d$model.pcorrect) * d$n.correct + log(1-d$model.pcorrect)*d$n.incorrect)))

						   
# Find human-vs-model correlations
A <- NULL 
for(q in split(d, d$conceptlist)) {
	A <- rbind(A, data.frame(conceptlist=q$conceptlist[1],
							 human.correct=mean(q$human.pcorrect),
							 model.correct=mean(q$model.pcorrect),							 
						     cor=cor.test(q$model.pcorrect, q$human.pcorrect)$estimate,
						     kendall=cor.test(q$model.pcorrect, q$human.pcorrect,method="kendall")$estimate))
}


# d <- subset(d, is.element(conceptlist, c("hg01L1", "hg02L1", "hg03L1", "hg04L1", "hg05L1", "hg06L1", "hg07L1", "hg08L1", "hg09L1", "hg10L1")))
						
plt <- ggplot(d, aes(x=item, y=model.pcorrect)) + 
	geom_line(aes(x=item, y=human.pcorrect), linewidth=0.5) +
	geom_line(color="red", linewidth=0.5, linetype="solid") + 
	facet_wrap(~conceptlist) + 
	theme_bw()
	
