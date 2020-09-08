
# file from https://openpsychologydata.metajnl.com/articles/10.5334/jopd.19/
d <- read.table("numbergame_data.tab", sep="\t", header=T)

# give sets a friendlier name
d$set <- gsub("_ ", ",", as.character(d$set))

# check out this dataset: table(d$set, d$target)

# now go throguh and compute
a <- aggregate( cbind(rating==1,rating==0) ~ target + set, d, sum)

names(a)[3:4] <- c("yes", "no")
write.table(a, "data.txt", row.names=F, col.names=T, quote=F, sep="\t")
# Output format is query, set (comma separated), yes ratings, no ratings
