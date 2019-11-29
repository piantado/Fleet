library(ggplot2)

NDATA <- 600 # the amount of data we computed on 

d <- read.table("out/out.txt", header=F)
names(d) <- c("KL1", "KL2", "recurse", "posterior", "prior", "likelihood",  "hypothesis")
d$likelihood.per <- d$likelihood / NDATA

remap = list("U.U.U.U.U.U.U.U.U"="Non-Knower",
             "1.U.U.U.U.U.U.U.U"="1-knower",
             "1.2.U.U.U.U.U.U.U"="2-knower",
             "1.2.3.U.U.U.U.U.U"="3-knower",
             "1.2.3.4.U.U.U.U.U"="4-knower",
             "1.2.3.4.5.6.7.8.9"="Full Counter")           

# Check if we include the ANS
d$ANS <- grepl("ANS", as.character(d$hypothesis))
             
# Define knower-levels from model output 
d$KnowerLevel <- ifelse(d$KL2 %in% names(remap), remap[as.character(d$KL2)], "Other")
d$KnowerLevel <- unlist(d$KnowerLevel) # R is such horseshit

print(table(d$KnowerLevel))

###################################################################################################
## Make Banana plot
###################################################################################################

# find the minima to plot the pareto edge
# NOTE: This does not compute the pareto edge, just the points which happen to have lowest prior in each knower level 
edge <- NULL
for(n in names(remap)) {
    q <- subset(d, KnowerLevel==remap[n])
    if(nrow(q) > 0) {
        q <- q[order(q$prior,decreasing=TRUE),]
        edge <- rbind(edge, data.frame(x=q[1,"prior"], y=q[1,"likelihood"], KnowerLevel=q[1,"KnowerLevel"]))
    }
}

plt <- ggplot(d, aes(x=-prior, y=-likelihood, color=KnowerLevel, group=KnowerLevel)) + 
    geom_jitter(aes(shape=ANS), alpha=0.7, height=5.0, width=0.0) +
    theme_bw() + theme(legend.position=c(.85,.75)) +
    scale_shape_manual(values=c(1,4)) +
    geom_line(data=edge, aes(x=-x, y=-y, group=1), color="black", size=1.5, linetype="dashed", alpha=0.5) +
    xlab("Prior penalty \t(worse \U27f6 )") + ylab("Fit to data \t(worse \U27f6 )")

cairo_pdf("banana.pdf", height=4, width=4) # Cairo needed for unicode arrow output
plt
dev.off()


plt <- ggplot(d, aes(x=prior, y=1.0/likelihood, color=KnowerLevel, group=KnowerLevel)) + 
    theme_bw() + theme(legend.position=c(.15,.55)) +
    scale_shape_manual(values=c(1,4))   +
    geom_point(aes(shape=ANS))
cairo_pdf("banana.pdf", height=4, width=6) # Cairo needed for unicode arrow output
plt
dev.off()

###################################################################################################
## Plot learning curves

logsumexp <- function(x) { m=max(x); log(sum(exp(x-m)))+m }

D <- NULL
for(amt in seq(1,600,5)) {

    d$newpost <- d$prior + amt*d$likelihood.per # compute a new approximate posterior by scaling the ll-per-datapoint 
    d$newpost <- exp(d$newpost - logsumexp(d$newpost)) #normalize and convert to probability
    a <- aggregate(newpost ~ KnowerLevel + ANS, d, sum)
    a$data.amount=amt
    
    D <- rbind(D, a)    
}

plt <- ggplot(subset(D,!ANS), aes(x=data.amount,y=newpost,color=KnowerLevel,group=KnowerLevel)) + 
    geom_line(width=1.5) + 
    geom_line(data=subset(D,ANS), linetype="dashed", width=1.5) +
    theme_bw() + theme(legend.position=c(.85,.5)) +
    xlab("Amount of data") + ylab("Posterior probability of knower-levels")

cairo_pdf("curves.pdf", height=3, width=6) # Cairo needed for unicode arrow output
plt
dev.off()


