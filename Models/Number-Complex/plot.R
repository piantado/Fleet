library(ggplot2)

NDATA <- 2000 # the amount of data we computed on 

logsumexp <- function(x) { m=max(x); log(sum(exp(x-m)))+m }

alld <- read.table("out/out.txt", header=F)
names(alld) <- c("type", "KL", "recurse", "posterior", "prior", "likelihood",  "hypothesis")

alld$likelihood.per <- alld$likelihood / NDATA

remap = list("U.U.U.U.U.U.U.U.U.U."="Non-Knower",
             "1.U.U.U.U.U.U.U.U.U."="1-knower",
             "1.2.U.U.U.U.U.U.U.U."="2-knower",
             "1.2.3.U.U.U.U.U.U.U."="3-knower",
             "1.2.3.4.U.U.U.U.U.U."="4-knower",
             "1.2.3.4.5.6.7.8.9.10."="Full Counter")           

# Check if we include the ANS
alld$ANS <- grepl("ANS", as.character(alld$hypothesis))
             
# Define knower-levels from model output 
alld$KnowerLevel <- ifelse(alld$KL %in% names(remap), remap[as.character(alld$KL)], "Other")
alld$KnowerLevel <- unlist(alld$KnowerLevel) # R is such horseshit


d <- subset(alld, type=="normal")

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


# di <- 200
for(di in c(0,100,500)) {
    plt <- ggplot(d, aes(x=-prior, y=-likelihood.per, color=prior+di*likelihood.per-logsumexp(prior+di*likelihood.per) )) + 
        geom_point(size=0.1) +
        theme_bw() + scale_color_gradient(low="blue", high="red", limits=c(-15,0)) +
        xlab("Prior penalty (worse \U27f6 )") + ylab("Fit to data (worse \U27f6 )") +
        theme(legend.position="none") +
        ggtitle(paste0(di, " data points"))
    ggsave(plt, filename=paste0("banana-",di,".pdf"), height=2.5, width=2.5, device = cairo_pdf) # Cairo needed for unicode arrow output
}

# Old style banana plot with hypothesis labels
# plt <- ggplot(d, aes(x=-prior, y=-likelihood, color=KnowerLevel, group=KnowerLevel)) + 
#     geom_jitter(aes(shape=ANS), alpha=0.7, height=5.0, width=0.0) +
#     theme_bw() + theme(legend.position=c(.75,.55)) +
#     scale_shape_manual(values=c(1,4)) +
#     #scale_x_continuous(trans='log2') +
#     #scale_y_continuous(trans='log2')+
# #     geom_line(data=edge, aes(x=-x, y=-y, group=1), color="black", size=1.5, linetype="dashed", alpha=0.5) +
#     xlab("Prior penalty (worse \U27f6 )") + ylab("Fit to data (worse \U27f6 )")
# 
# 
# cairo_pdf("banana.pdf", height=4.5, width=4.5) # Cairo needed for unicode arrow output
# plt
# dev.off()

# 
# plt <- ggplot(d, aes(x=prior, y=1.0/likelihood, color=KnowerLevel, group=KnowerLevel)) + 
#     theme_bw() + theme(legend.position=c(.15,.55)) +
#     scale_shape_manual(values=c(1,4))   +
#     geom_point(aes(shape=ANS))
# cairo_pdf("banana.pdf", height=4, width=6) # Cairo needed for unicode arrow output
# plt
# dev.off()

###################################################################################################
## Plot learning curves


make_learning_curves <- function(q) {
    D <- NULL
    for(amt in seq(1,800,5)) {

        q$newpost <- q$prior + amt*q$likelihood.per # compute a new approximate posterior by scaling the ll-per-datapoint 
        q$newpost <- exp(q$newpost - logsumexp(q$newpost)) #normalize and convert to probability
        a <- aggregate(newpost ~ KnowerLevel + ANS, q, sum)
        a$data.amount=amt
        
        D <- rbind(D, a)    
    }
    return(D)
}


D <- make_learning_curves(subset(alld, type=="normal"))


plt <- ggplot(D, aes(x=data.amount,y=newpost,color=KnowerLevel,group=KnowerLevel)) + 
    geom_line(width=1.5, data=subset(D,!ANS)) + 
    geom_line(data=subset(D,ANS), linetype="dashed", width=1.5) +
    theme_bw() + theme(legend.position=c(.85,.5)) +
    xlab("Amount of data") + ylab("Posterior probability of knower-levels")

ggsave("curves.pdf",height=3, width=7)

# cairo_pdf("curves.pdf", height=3, width=6) # Cairo needed for unicode arrow output
# plt
# dev.off()
# 
# ###################################################################################################
# ## Plot with variable kinds of data
# 
# Dlt <- make_learning_curves(subset(alld, type=="lt4"))
# 
# plt <- ggplot(Dlt, aes(x=data.amount,y=newpost,color=KnowerLevel,group=KnowerLevel)) + 
#     geom_line(width=1.5, data=subset(Dlt,!ANS)) + 
#     geom_line(data=subset(Dlt,ANS), linetype="dashed", width=1.5) +
#     theme_bw() + theme(legend.position=c(.85,.5)) +
#     xlab("Amount of data") + ylab("Posterior probability of knower-levels")
#     
# Dgt <- make_learning_curves(subset(alld, type=="gt4"))
# 
# plt <- ggplot(Dgt, aes(x=data.amount,y=newpost,color=KnowerLevel,group=KnowerLevel)) + 
#     geom_line(width=1.5, data=subset(Dgt,!ANS)) + 
#     geom_line(data=subset(Dgt,ANS), linetype="dashed", width=1.5) +
#     theme_bw() + theme(legend.position=c(.85,.5)) +
#     xlab("Amount of data") + ylab("Posterior probability of knower-levels")

