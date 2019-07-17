
import pandas
import re

sortOut = './KidAlgorithmTerminalLog/ModelLog_SortEditDis.txt'
doubleOut = './KidAlgorithmTerminalLog/ModelLog_DoubleEditDis.txt'
hitchOut = './KidAlgorithmTerminalLog/ModelLog_HitchEditDis.txt'
sortAlgoType = "Sort"
doubleAlgoType = "Double"
hitchAlgoType = "Hitch"
sortDemo = "R/,/G\;R/,/G,R/,/G\;R/,/G,R/,/G,R/,/G"
doubleDemo = "G/,/RR\;G/,/RR,G/,/RR\;G/,/RR,G/,/RR,G/,/RR"
hitchDemo = "/RB,GB/\;/RB,GB/,/RB,GB/\;/RB,GB/,/RB,GB/,/RB,GB/"

csvOutPath = './editDis.csv'


sortStr = ''
doubleStr = ''
hitchStr = ''
sortDict = {}
doubleDict = {}
hitchDict = {}
with open(sortOut,'r') as f:
	sortStr = f.read()
with open(doubleOut,'r') as f:
	doubleStr = f.read()
with open(hitchOut,'r') as f:
	hitchStr = f.read()

newData = {'hypIndex':[], 'Hypothesis':[],'posteriorScore':[], 'ALGOTYPE':[],'DEMO':[],'STEP_Num':[], 'BallMoveCnt':[], 'Bucket':[],'RESPONSE':[]}

sortResult = re.findall('\n-[0-9].*"',sortStr)
doubleResult = re.findall('\n-[0-9].*"',doubleStr)
hitchResult = re.findall('\n-[0-9].*"',hitchStr)

def cleanupAlgoData (algoResult, algoDataDict):
	for index, item in enumerate(algoResult): 
		postS, actStr = re.split(r"\t", item.strip('\n'))
		actStr = actStr.strip('"')
		errAct = 0
		actStrActs = actStr.split(",")
		for i in actStrActs: 
			if not('/' in i and i.count('/') == 1 and ('R' in i or 'G' in i or 'B' in i)):
				errAct = 1
				break
		if errAct == 0: 
			# print actStr
			algoDataDict[actStr] = float(postS)

def writeDict (algoDataDict, algotype, demo, outputDict): 
	for index, (hypo, postS) in enumerate(sorted(algoDataDict.iteritems(),key=lambda(k,v): (v,k), reverse=True)):
		hypIndex = index
		algotype = algotype
		demo = demo
		hypothesis = hypo
		posteriorScore = postS
		actStr = hypo.split(',')
		BallMoveCnt = 0
		for index, i in enumerate(actStr): 
			stepNum = index
			if '/' in i and i.count('/') == 1 and ('R' in i or 'G' in i or 'B' in i):
				lb, rb = re.split(r"/", i)
				if len(lb): 
					for i in list(lb):
						outputDict['hypIndex'].append(hypIndex)
						outputDict['Hypothesis'].append(hypothesis)
						outputDict['posteriorScore'].append(posteriorScore)
						outputDict['ALGOTYPE'].append(algotype)
						outputDict['DEMO'].append(demo)
						outputDict['STEP_Num'].append(stepNum)
						outputDict['Bucket'].append('left')
						outputDict['RESPONSE'].append(i)
						outputDict['BallMoveCnt'].append(BallMoveCnt)
						BallMoveCnt += 1
				if len(rb): 
					for i in list(rb):
						outputDict['hypIndex'].append(hypIndex)
						outputDict['Hypothesis'].append(hypothesis)
						outputDict['posteriorScore'].append(posteriorScore)
						outputDict['ALGOTYPE'].append(algotype)
						outputDict['DEMO'].append(demo)
						outputDict['STEP_Num'].append(stepNum)
						outputDict['Bucket'].append('right')
						outputDict['RESPONSE'].append(i)
						outputDict['BallMoveCnt'].append(BallMoveCnt)
						BallMoveCnt += 1

# cleanupAlgoData(sortResult, sortDict)
# writeDict(sortDict, sortAlgoType, sortDemo, newData)

cleanupAlgoData(doubleResult, doubleDict)
writeDict(doubleDict, doubleAlgoType, doubleDemo, newData)

# cleanupAlgoData(hitchResult, hitchDict)
# writeDict(hitchDict, hitchAlgoType, hitchDemo, newData)



import pandas as pd
df = pd.DataFrame(data=newData)
df.to_csv(path_or_buf=csvOutPath, index=False)
