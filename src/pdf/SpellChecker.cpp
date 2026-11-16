// 拼写检查：内置常用词表 + 启发式规则 + 可加载的用户词典。
//
// 为什么是这个口径（而不是完整词典）：
// 仓库里没有可用的词典资源，也不允许为此引入新依赖、联网下载。
// 完整英文词典是十万词量级的数据，凭空手写一份既不现实、也不可靠。
// 所以这里内置一千个左右的高频词，再用启发式规则补上「明显可疑」的词，
// 最后允许用户挂自己的词典（Hunspell .dic 子集）。
//
// 为什么漏判可以接受、误判不行：
// 词表覆盖不到的词（专有名词、技术术语）会被漏判——这只是少画一条波浪线；
// 而误判会在满篇正常文字里到处画红线，用户很快就不信这条波浪线了。
// 因此下面所有规则都只盯「明显可疑」的词：常见拼错对、连续重复的词、
// 与常用词只差一个编辑的近邻词、句中大小写异常。

#include "pdf/SpellChecker.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <QVector>

#include <algorithm>
#include <limits>

namespace aipdf::pdf {
namespace {

// 内置常用词表（小写存储；按使用频率大致排序，排在前面 = 更常见，
// 建议列表按这个次序出，所以高频词会排在前面）。
//
// 这**不是**完整词典：它只覆盖最常用的一千个左右的英文词。
// 好处是零依赖、零体积、行为可预测；代价是生词会被漏判。
const char* const kBuiltinWords[] = {
    "the","be","to","of","and","a","in","that",
    "have","i","it","for","not","on","with","he",
    "as","you","do","at","this","but","his","by",
    "from","they","we","say","her","she","or","an",
    "will","my","one","all","would","there","their","what",
    "so","up","out","if","about","who","get","which",
    "go","me","when","make","can","like","time","no",
    "just","him","know","take","people","into","year","your",
    "good","some","could","them","see","other","than","then",
    "now","look","only","come","its","over","think","also",
    "back","after","use","two","how","our","work","first",
    "well","way","even","new","want","because","any","these",
    "give","day","most","us","is","are","was","were",
    "been","has","had","did","said","made","find","where",
    "thing","many","much","may","part","such","keep","own",
    "last","long","great","little","old","right","big","high",
    "different","small","large","next","early","young","important","few",
    "public","bad","same","able","man","woman","child","eye",
    "place","week","case","point","government","company","number","group",
    "problem","fact","question","world","life","hand","head","room",
    "mother","area","money","story","month","lot","study","book",
    "job","word","business","issue","side","kind","house","service",
    "friend","father","power","hour","game","line","end","member",
    "law","car","city","community","name","president","team","minute",
    "idea","kid","body","information","parent","face","others","level",
    "office","door","health","person","art","war","history","party",
    "result","change","morning","reason","research","girl","guy","moment",
    "air","teacher","force","education","foot","boy","age","policy",
    "process","music","market","sense","nation","plan","college","interest",
    "death","experience","effect","class","control","care","field","development",
    "role","effort","rate","heart","drug","show","leader","light",
    "voice","wife","above","across","actually","administration","affect","again",
    "against","agent","ago","ahead","aircraft","almost","alone","along",
    "already","although","always","american","among","angry","another","anyone",
    "anything","anywhere","approach","around","attitude","available","away","basic",
    "basis","beautiful","become","bedroom","behalf","behind","belief","believe",
    "below","beneath","beside","best","better","between","beyond","black",
    "blood","blue","born","both","bother","brain","brave","brief",
    "bright","bring","broad","broken","build","busy","buy","cancer",
    "candidate","careful","central","certainly","cheap","childhood","claim","clothes",
    "cold","combination","common","competition","connection","constant","contract","cool",
    "courage","crazy","creative","creature","criminal","crisis","critical","cultural",
    "curious","dark","dawn","deaf","deep","defeat","defend","define",
    "definitely","description","designer","desperate","dirty","discipline","disease","dish",
    "dismiss","distant","distinguish","down","downstairs","due","dull","during",
    "each","eager","ease","easily","effective","efficient","eight","elderly",
    "elect","else","elsewhere","emerge","emergency","employee","engage","enough",
    "environment","equal","especially","essential","establish","estimate","evening","eventually",
    "ever","every","everybody","everyday","everything","everywhere","exact","exactly",
    "exceed","excellent","except","execute","experiment","extra","extreme","false",
    "familiar","famous","far","fast","fat","final","fine","five",
    "former","forward","four","fresh","friendly","friendship","further","gas",
    "gentleman","glad","global","god","grandfather","grant","gray","grey",
    "grow","happy","hard","hardly","headquarters","hear","heavy","hello",
    "helpful","hence","here","heritage","herself","highway","himself","hip",
    "hire","hollow","honest","hot","household","however","huge","humor",
    "ideal","ill","illegal","immediate","immediately","immigrant","implement","imply",
    "impossible","indeed","independence","independent","individual","industrial","initial","inside",
    "instance","instead","institution","instruction","instrument","intelligence","intention","interesting",
    "internal","internet","interpret","interview","investigate","joke","junior","lack",
    "late","later","least","left","legal","likely","local","loose",
    "lost","lucky","mad","mainly","maybe","mayor","mean","meaning",
    "meanwhile","mechanism","medical","mere","modern","more","moreover","motion",
    "movement","murder","myself","mystery","narrow","national","native","natural",
    "near","necessary","negative","neither","nervous","never","nevertheless","newly",
    "nice","nine","nobody","nod","none","nor","normal","nothing",
    "nowhere","numerous","obvious","obviously","odd","off","often","oh",
    "okay","once","onto","opposite","ordinary","original","otherwise","ought",
    "ourselves","outside","overcome","overlook","pale","park","particular","particularly",
    "partly","partner","passage","passenger","past","path","pattern","pay",
    "peace","pen","pencil","per","percent","perfect","perhaps","permanent",
    "permission","pet","phrase","physical","piano","pilot","pipe","pitch",
    "plain","plane","planet","plastic","plate","platform","pleasure","plenty",
    "plot","plus","pocket","political","politics","poll","pool","poor",
    "pop","popular","port","portion","pose","positive","possible","pot",
    "potato","potential","pound","poverty","powder","practical","pray","precious",
    "pregnant","pressure","pretty","prime","prince","prior","priority","probably",
    "proceed","professor","profile","promote","proof","proper","properly","proportion",
    "proposal","prospect","protein","protest","proud","province","publication","punishment",
    "pure","purpose","pursue","put","queen","query","quiet","quietly",
    "rank","rapid","rare","rather","ratio","reader","reality","really",
    "recently","reference","reform","region","regular","relation","relative","relevant",
    "reliable","relief","religion","religious","remark","reputation","resemble","resident",
    "resource","response","responsibility","restaurant","restore","retain","revenue","reverse",
    "revolution","rhythm","rice","rich","ridge","rifle","river","rock",
    "roof","root","rope","rose","rough","round","route","routine",
    "row","run","runner","sacrifice","sad","safe","safety","salary",
    "sale","salt","sample","saw","scandal","scared","scene","scheme",
    "scholar","school","science","screen","second","secret","secretary","secure",
    "security","seed","seem","seize","senior","sequence","series","serious",
    "set","settlement","seven","several","severe","sew","shade","shadow",
    "shallow","shot","shy","sick","sigh","significant","silent","since",
    "slightly","slowly","somebody","somehow","someone","something","sometimes","somewhat",
    "somewhere","sophisticated","sorry","specific","speech","spite","sponsor","sweat",
    "tall","technical","temporary","ten","tendency","terms","themselves","thirsty",
    "those","throughout","tight","till","tired","too","tough","toward",
    "transition","transportation","treatment","truck","truly","ultimate","unable","unfair",
    "unfortunately","uniform","urgent","vast","very","veteran","virtual","wage",
    "western","whatever","whenever","whereas","wherever","whom","widely","willing",
    "withdraw","wonderful","writing","yeah","yes","yours","yourself","zero",
    "police","mind","finally","pull","return","free","military","price",
    "report","less","according","decision","explain","son","hope","develop",
    "view","relationship","carry","town","road","drive","arm","true",
    "federal","break","difference","thank","receive","value","international","building",
    "action","full","model","join","season","society","tax","director",
    "position","player","record","paper","space","ground","form","support",
    "event","official","whose","matter","everyone","center","couple","site",
    "project","hit","base","activity","star","table","need","court",
    "produce","eat","teach","oil","half","situation","easy","cost",
    "industry","figure","street","image","itself","phone","either","data",
    "cover","quite","picture","clear","practice","piece","land","recent",
    "describe","product","doctor","wall","patient","worker","news","test",
    "movie","certain","north","personal","simply","third","technology","catch",
    "step","baby","computer","type","attention","draw","film","republican",
    "tree","source","red","nearly","choose","cause","hair","century",
    "evidence","window","difficult","listen","soon","culture","billion","chance",
    "brother","energy","period","summer","real","hill","fire","south",
    "provide","box","temperature","state","skin","scientist","tool","low",
    "wide","compare","quickly","sound","foreign","entire","done","english",
    "west","clearly","standard","bill","message","read","allow","simple",
    "range","stay","length","subject","private","quick","jump","heat",
    "club","machine","whole","king","size","card","civil","love",
    "coffee","population","unit","rule","scale","lake","shape","grade",
    "edge","sort","signal","release","spring","observe","chief","section",
    "weather","bear","letter","condition","choice","single","female","sir",
    "floor","church","column","daughter","desire","move","page","country",
    "protect","though","farm","worth","stage","staff","technique","texture",
    "union","committee","fashion","task","wash","should","captain","ticket",
    "wish","agency","supply","customer","surgery","phase","manner","wire",
    "exhibit","brand","benefit","ancient","status","crime","impact","loan",
    "tension","era","mission","enterprise","budget","killer","sector","reflection",
    "gospel","tune","gene","tissue","cell","tank","commercial","bicycle",
    "traffic","barn","besides","kilometre","bureau","republic","talent","cycle",
    "accept","access","achieve","act","add","address","admit","advance",
    "advise","afford","agree","appear","apply","argue","arrange","arrive",
    "ask","assist","assume","attach","attack","attempt","attend","attract",
    "avoid","bake","balance","ban","bathe","battle","beam","beg",
    "behave","belong","bend","bet","bind","bite","blame","bleed",
    "blend","blind","block","blow","boil","bomb","bore","borrow",
    "bounce","bow","breathe","breeze","brush","bubble","bump","burn",
    "bury","calculate","call","camp","cancel","challenge","charge","chase",
    "cheat","check","cheer","chew","clap","clean","climb","cling",
    "close","coach","collect","combine","communicate","compete","complain","complete",
    "concentrate","concern","confess","confuse","connect","consider","consist","contain",
    "continue","contrast","contribute","convince","cook","copy","correct","cough",
    "count","crack","crash","crawl","create","cross","cry","cure",
    "curl","damage","dance","dare","decide","declare","decline","decorate",
    "delay","deliver","demand","deny","depart","depend","deserve","design",
    "destroy","detail","detect","devote","die","dig","direct","disagree",
    "disappear","disappoint","discover","discuss","divide","doubt","drag","drain",
    "dream","dress","drift","drink","drop","drown","drum","dry",
    "dust","earn","educate","embarrass","employ","empty","encourage","enjoy",
    "enter","entertain","escape","examine","excite","excuse","exercise","exist",
    "expand","expect","explore","export","express","extend","fade","fail",
    "fancy","fasten","fear","feed","feel","fetch","fight","fill",
    "finish","fit","fix","flatter","flee","float","flow","fold",
    "follow","forget","forgive","found","freeze","frighten","gather","generate",
    "gentle","glance","glow","govern","grab","grasp","greet","grin",
    "grip","groan","guarantee","guard","guess","guide","hammer","handle",
    "hang","happen","harass","harm","hate","haunt","heal","help",
    "hesitate","hide","hold","hook","hover","hug","hum","hunt",
    "hurry","hurt","identify","ignore","imagine","impress","improve","include",
    "increase","indicate","influence","inform","insist","inspect","install","intend",
    "interfere","interrupt","introduce","invent","invite","involve","judge","kick",
    "kill","kiss","knock","knot","label","laugh","launch","lay",
    "lead","lean","leap","learn","leave","lend","let","lie",
    "lift","limit","link","list","live","load","lock","lose",
    "lower","maintain","manage","mark","marry","match","measure","meet",
    "melt","mention","miss","mix","multiply","neglect","note","notice",
    "obey","object","obtain","occur","offer","open","operate","oppose",
    "order","organize","owe","pack","paint","participate","pass","paste",
    "pause","peel","perform","permit","persist","persuade","pick","pile",
    "pin","plant","play","please","plug","polish","possess","post",
    "pour","praise","predict","prefer","prepare","present","preserve","press",
    "pretend","prevent","print","program","promise","pronounce","propose","prove",
    "publish","punish","purchase","push","qualify","queue","quit","quote",
    "race","raise","reach","react","realize","rebuild","recall","recite",
    "recognize","recommend","recover","reduce","refer","reflect","refuse","regard",
    "register","regret","reject","relate","relax","rely","remain","remember",
    "remind","remove","repair","repeat","replace","reply","represent","request",
    "require","rescue","reserve","resist","respect","respond","rest","retire",
    "reveal","review","reward","rid","ride","ring","rise","risk",
    "roll","rub","rush","sail","satisfy","save","scan","scare",
    "schedule","score","scream","search","seat","seek","select","sell",
    "send","separate","serve","settle","shake","share","shave","shout",
    "shut","sign","sing","sink","sit","sleep","slide","smell",
    "smile","snap","sniff","snow","soak","solve","speak","spell",
    "spend","spill","spin","spit","split","spoil","spot","spray",
    "spread","squeeze","stack","stain","stare","start","steal","stick",
    "stir","stop","store","storm","stretch","strike","struggle","stumble",
    "suppose","surprise","surround","survive","suspect","swallow","swear","sweep",
    "swim","swing","switch","talk","taste","tear","tell","terrify",
    "threat","throw","tidy","tie","touch","trade","train","transfer",
    "transform","translate","transport","trap","travel","treat","tremble","trust",
    "try","turn","twist","understand","undo","unite","update","urge",
    "vanish","vary","visit","vote","wait","wake","walk","wander",
    "warm","warn","waste","watch","water","wave","weigh","welcome",
    "whisper","win","wipe","wonder","worry","wrap","write","wrong",
    "yawn","yield","zoom","account","actor","adult","advice","afternoon",
    "agreement","airport","alarm","album","amount","anger","animal","answer",
    "apple","argument","army","arrival","article","artist","aspect","assembly",
    "assignment","audience","author","authority","average","award","background","bag",
    "ball","band","bank","bar","basket","bath","beach","beat",
    "bed","beef","beer","beginning","bell","belt","bench","berry",
    "bike","bird","birth","birthday","bit","blade","blanket","blank",
    "blossom","board","boat","bone","boot","border","boss","bottle",
    "bottom","bowl","branch","bread","breakfast","breath","brick","bridge",
    "brown","bucket","bulb","bundle","burden","bus","bush","butter",
    "button","cabin","cable","cage","cake","calendar","camera","campaign",
    "candle","candy","canvas","cap","capital","career","cargo","carpet",
    "carriage","cart","cash","castle","cat","cattle","cave","cement",
    "census","cent","chain","chair","chalk","channel","chapter","character",
    "charity","chart","cheek","cheese","cherry","chest","chicken","chin",
    "circle","citizen","clay","clerk","cliff","climate","clock","cloth",
    "cloud","coal","coast","coat","code","coin","collar","color",
    "comb","combat","comfort","command","comment","concert","conference","confidence",
    "conflict","contact","content","contest","context","conversation","corn","corner",
    "cottage","cotton","council","counter","coupon","course","cousin","cow",
    "craft","cream","credit","crew","crop","crowd","crown","crystal",
    "cup","curtain","curve","cushion","custom","cut","daily","dancer",
    "danger","date","dead","deal","dear","debate","debt","decade",
    "deck","deed","defect","degree","delivery","denial","dentist","depth",
    "desert","desk","device","diagram","diamond","diet","dinner","direction",
    "directory","dirt","dispute","distance","district","ditch","dog","doll",
    "dollar","dome","donor","dose","dot","dough","dozen","draft",
    "drama","drawer","driver","duck","duty","eagle","ear","earth",
    "east","editor","egg","elbow","elder","election","electric","elephant",
    "elevator","elite","embassy","emotion","empire","employer","enemy","engine",
    "engineer","entry","envelope","envy","episode","equipment","error","essay",
    "estate","exam","example","exchange","excitement","exit","expense","expert",
    "expression","fabric","facility","factor","factory","failure","fair","faith",
    "fall","family","fan","farmer","fault","favor","feast","fee",
    "feedback","feeling","fellow","fence","festival","fever","file","filter",
    "finance","finger","firm","fish","fist","flag","flame","flash",
    "flat","flavor","flesh","flight","flood","flour","flower","flu",
    "fluid","flute","fly","focus","folk","food","fool","football",
    "forest","fork","fortune","forum","foundation","fountain","fourth","fox",
    "frame","freedom","freight","front","fruit","fuel","fun","function",
    "fund","funeral","furniture","future","gain","galaxy","gallery","gang",
    "garage","garbage","garden","gate","gear","general","generation","gesture",
    "ghost","giant","gift","glacier","glass","glove","glue","goal",
    "goat","gold","golf","goods","goose","governor","grain","grand",
    "grandmother","grape","graph","grass","grave","gravity","grease","green",
    "grocery","growth","guest","guitar","gun","gym","habit","hall",
    "harbor","hat","height","helmet","hero","hobby","hole","holiday",
    "home","honey","honor","horizon","horse","hospital","host","hotel",
    "housing","human","husband","ice","importance","income","infant","inflation",
    "injury","ink","insect","insurance","interior","investment","invitation","iron",
    "island","item","jacket","jail","jar","jaw","jeans","joint",
    "journal","journey","joy","juice","junction","jury","justice","keen",
    "kettle","key","keyboard","kidney","kitchen","knee","knife","knight",
    "knowledge","lab","labor","ladder","lady","lamp","landscape","lane",
    "language","lantern","lap","laptop","laughter","lawyer","layer","leaf",
    "league","leather","lecture","leg","legend","leisure","lemon","lens",
    "lesson","lever","library","license","lid","lion","lip","liquid",
    "liter","literature","lobby","log","logic","loop","lord","loss",
    "loud","luck","lunch","lung","magazine","magic","mail","main",
    "major","male","mall","manager","mantle","manual","manufacturer","map",
    "march","margin","marriage","mask","mass","master","material","meal",
    "meat","mechanic","medal","media","medicine","meeting","memory","men",
    "menu","mercy","mess","metal","method","middle","midnight","mile",
    "milk","mill","mine","minister","minor","mirror","mistake","mixture",
    "mobile","modem","monitor","monkey","mood","moon","moral","motor",
    "mountain","mouse","mouth","mud","muscle","museum","nail","nature",
    "neck","needle","neighbor","nerve","nest","net","network","newspaper",
    "night","noise","nose","novel","nurse","nut","oak","oath",
    "obligation","observation","occasion","occupation","ocean","officer","opening","operation",
    "opinion","opportunity","opposition","option","orange","orbit","organization","origin",
    "outcome","output","overcoat","overhead","owner","ox","pace","package",
    "pain","pair","palace","palm","pan","panel","panic","parade",
    "paragraph","parallel","personality","perspective","petition","petrol","phenomenon","philosophy",
    "photo","pride","priest","primary","principle","prison","privacy","prize",
    "procedure","profession","profit","progress","property","shame","shark","sharp",
    "shed","sheep","sheer","sheet","shelf","shell","shelter","shield",
    "shift","shine","ship","shirt","shock","shoe","shoot","shop",
    "shore","short","shoulder","shove","shower","shrimp","shrink","shrug",
    "shuttle","sibling","sight","signature","silence","silk","silver","similar",
    "sin","sincere","singer","sister","six","skeleton","sketch","ski",
    "skill","skirt","sky","slave","slice","slight","slim","slip",
    "slope","slot","slow","smart","smoke","smooth","snake","soap",
    "social","sock","soft","soil","soldier","sole","solid","solution",
    "song","sorrow","soul","soup","spare","spark","sparrow","speaker",
    "special","species","specimen","spectacle","speed","sphere","spice","spider",
    "spike","spirit","spoon","sport","spouse","spy","square","stability",
    "stair","stake","stall","stamp","stand","staple","statement","station",
    "statue","steady","steam","steel","steep","steer","stem","stiff",
    "still","stimulus","sting","stock","stomach","stone","storage","stove",
    "straight","strain","strange","stranger","strategy","straw","stream","strength",
    "stress","strict","string","strip","stripe","stroke","strong","structure",
    "student","studio","stuff","stump","stupid","style","submit","substance",
    "substitute","subtle","suburb","succeed","success","sudden","suffer","sufficient",
    "sugar","suggest","suit","suitable","sum","summary","summit","sun",
    "super","superior","supermarket","supervisor","supper","supreme","sure","surface",
    "surgeon","surplus","survey","suspend","suspicion","sustain","swamp","sweater",
    "sweet","swell","swift","sword","symbol","sympathy","symptom","syndrome",
    "synthesis","system","tablet","tackle","tactic","tail","tailor","tale",
    "tap","tape","target","taxi","tea","tease","teenager","telephone",
    "television","temper","temple","tenant","tend","tender","tennis","tent",
    "term","terminal","terrace","terrain","terrible","territory","terror","testimony",
    "text","theater","theme","theory","therapy","therefore","thermal","thick",
    "thief","thin","thirst","thorough","thought","thousand","thread","three",
    "threshold","thrive","throat","throne","through","thumb","thunder","thus",
    "tide","tiger","tile","timber","tin","tiny","tip","tire",
    "title","toast","tobacco","today","toe","together","toilet","token",
    "tolerance","tomato","tomorrow","tone","tongue","tonight","tooth","top",
    "topic","torch","torture","total","tour","tourist","towel","tower",
    "toy","trace","track","tradition","tragic","trail","trait","transaction",
    "transit","tray","treasure","trend","trial","triangle","tribe","trick",
    "trigger","trip","triumph","troop","trophy","tropic","trouble","trumpet",
    "trunk","truth","tube","tunnel","turkey","turtle","tutor","twelve",
    "twenty","twice","twin","typical","ugly","umbrella","uncle","under",
    "undergo","undertake","underwear","unique","unity","universe","university","unknown",
    "unless","unlike","until","unusual","upon","upper","upset","urban",
    "usage","useful","user","usual","utility","vacation","valid","valley",
    "van","variety","various","vegetable","vehicle","veil","vein","vendor",
    "venture","venue","verb","verdict","verse","version","victim","victory",
    "video","village","violence","virtue","virus","visible","vision","visitor",
    "visual","vital","vitamin","volume","volunteer","voter","voyage","waist",
    "waiter","wallet","ward","warden","wardrobe","warning","washer","wax",
    "weak","weakness","wealth","weapon","wear","weave","web","website",
    "wedding","weekend","weight","welfare","wet","whale","wheat","wheel",
    "whether","while","white","why","widow","width","wild","wind",
    "wine","wing","winner","winter","wisdom","wise","wit","witch",
    "within","without","witness","wolf","wood","wool","workshop","worm",
    "worse","worship","wound","wreck","wrench","wrist","writer","yard",
    "yarn","yell","yellow","yesterday","yet","yogurt","yoke","younger",
    "youth","zone","zoo","etc","eg","ie","vs","mr",
    "mrs","ms","dr","prof","inc","ltd","co","corp",
    "dept","est","fig","vol","approx","min","max","avg",
    "std","sec","jan","feb","mar","apr","jun","jul",
    "aug","sep","sept","oct","nov","dec","don't","doesn't",
    "can't","won't","isn't","aren't","wasn't","weren't","hasn't","haven't",
    "hadn't","couldn't","shouldn't","wouldn't","mustn't","shan't","it's","that's",
    "there's","here's","what's","who's","let's","i'm","i'll","i've",
    "i'd","we're","we'll","we've","we'd","they're","they'll","they've",
    "they'd","he'll","she'll","he's","she's","you're","you've","you'd",
    "you'll","ain't","cannot","must","shall","might","pdf","url",
    "html","xml","json","csv","api","app","apps","os",
    "pc","ui","ux","id","ok","n/a","self","email",
    "webpage","online","offline","username","filename","folder","directories","files",
    "document","documents","pages","texts","font","fonts","words","lines",
    "letters","paragraphs","sentence","sentences","characters","tables","images","pictures",
    "graphs","drawing","drawings","vector","raster","bitmap","alpha","beta",
    "gamma","layers","cropping","rotate","rotation","merge","merging","splitting",
    "extract","extraction","insert","insertion","delete","deletion","edit","editing",
    "viewers","viewer","browse","browsing","searching","replacing","replacement","spelling",
    "dictionary","dictionaries","bookmark","bookmarks","hyperlink","hyperlinks","annotation","annotations",
    "comments","highlight","highlights","signatures","encrypt","encrypted","encryption","decrypt",
    "password","passwords","shortcut","shortcuts","toolbar","toolbars","menus","dialog",
    "dialogs","buttons","icon","icons","windows","click","clicked","clicking",
    "redo","saved","saving","opened","closed","closing","printed","printing",
    "printer","copied","copying","pasted","pasting","zip","zipped","unzip",
    "download","downloaded","upload","uploaded","exported","import","imported","default",
    "defaults","setting","settings","options","devices","server","servers","client",
    "clients","networks","browser","browsers","database","databases","script","scripts",
    "template","templates","widget","widgets","checkbox","checkboxes","radio","dropdown",
    "metadata","quality","margins","format","formats","pixel","pixels","dpi",
    "megabyte","megabytes","gigabyte","gigabytes","kilobyte","byte","bytes","kilogram",
    "grams","meter","meters","centimeter","kilometer","inch","inches","miles",
    "yards","ounce","pounds","hundred","million","trillion","quarter","double",
    "triple","boolean","integer","array","arrays","objects","classes","methods",
    "functions","variable","variables","values","errors","warnings","messages","results",
    "items","lists","names","numbers","sizes","types","versions","states",
    "levels","points","angle","axis","matrix","component","components","element",
    "elements","feature","features","module","modules","packages","releases","updates",
    "upgrade","licenses","copyright","department","authors","customers","projects","plans",
    "tasks","goals","issues","solutions","examples","tests","testing","debug",
    "debugging","logs","logging","cache","buffer","buffers","queues","threads",
    "processes","session","sessions","events","signals","slots","layout","layouts",
    "styles","themes","palette","colors","colour","colours","families","bold",
    "italic","underline","align","alignment","padding","spacing","gap","borders",
    "radius","opacity","hidden","enable","disable","enabled","disabled","active",
    "inactive","current","previous","begin","ready","pending","running","finished",
    "failed","info","assert","actual","passed","skip","skipped","todo",
    "fixme","notes","readme","changelog","contributing","codes","doc","docs",
    "documentation","manuals","guides","tutorial","tutorials","faq","appendix","sections",
    "figures","diagrams","charts","index","indexes","indices","glossary","overview",
    "introduction","conclusion","methodology","analysis","discussion","limitations",
};

struct MisspellingPair {
    const char* wrong;
    const char* right;
};

// 常见拼错对：这些词即使不在词表里也一定要报出来。
// 它们是最常见的一批手误，命中率远高于「词典里没有」这条泛化规则。
const MisspellingPair kCommonMisspellings[] = {
    {"recieve","receive"}, {"recieved","received"}, {"recieving","receiving"},
    {"seperate","separate"}, {"seperated","separated"}, {"seperately","separately"},
    {"definately","definitely"}, {"definitly","definitely"}, {"definatly","definitely"},
    {"occured","occurred"}, {"occuring","occurring"}, {"occurence","occurrence"},
    {"wich","which"}, {"teh","the"}, {"adn","and"}, {"taht","that"},
    {"thier","their"}, {"becuase","because"}, {"becasue","because"},
    {"seperatly","separately"}, {"neccessary","necessary"}, {"necesary","necessary"},
    {"accomodate","accommodate"}, {"acheive","achieve"}, {"arguement","argument"},
    {"begining","beginning"}, {"beleive","believe"}, {"beleived","believed"},
    {"buisness","business"}, {"calender","calendar"}, {"cemetary","cemetery"},
    {"collegue","colleague"}, {"comming","coming"}, {"commited","committed"},
    {"concious","conscious"}, {"curiousity","curiosity"}, {"dissapoint","disappoint"},
    {"dissapointed","disappointed"}, {"embarass","embarrass"}, {"enviroment","environment"},
    {"existance","existence"}, {"experiance","experience"}, {"familar","familiar"},
    {"finaly","finally"}, {"foriegn","foreign"}, {"foward","forward"},
    {"freind","friend"}, {"fullfil","fulfill"}, {"goverment","government"},
    {"gaurd","guard"}, {"grammer","grammar"}, {"happend","happened"},
    {"harrass","harass"}, {"independant","independent"}, {"knowlege","knowledge"},
    {"liesure","leisure"}, {"maintainance","maintenance"}, {"occassion","occasion"},
    {"persistant","persistent"}, {"posession","possession"}, {"pratice","practice"},
    {"prefered","preferred"}, {"priviledge","privilege"}, {"publically","publicly"},
    {"realy","really"}, {"refered","referred"}, {"relevent","relevant"},
    {"religous","religious"}, {"rember","remember"}, {"succesful","successful"},
    {"succesfully","successfully"}, {"suprise","surprise"}, {"suprised","surprised"},
    {"tommorow","tomorrow"}, {"truely","truly"}, {"untill","until"},
    {"unfortunatly","unfortunately"}, {"wierd","weird"}, {"writting","writing"},
    {"alot","a lot"}, {"becuase","because"}, {"cant","can't"}, {"dont","don't"},
    {"doesnt","doesn't"}, {"isnt","isn't"}, {"wont","won't"}, {"wouldnt","wouldn't"},
    {"shoudl","should"}, {"coudl","could"}, {"woudl","would"}, {"theres","there's"},
    {"becasue","because"}, {"beleive","believe"}, {"gramar","grammar"},
};

// 缩写：带点的短词不参与拼写检查（"e.g."、"Mr." 这类）。
const char* const kAbbreviations[] = {
    "etc","eg","ie","vs","mr","mrs","ms","dr","prof","inc","ltd","co","corp","dept","est","fig","vol",
    "approx","min","max","avg","std","sec","jan","feb","mar","apr","jun","jul","aug","sep","sept","oct",
    "nov","dec","no","al","pp","cf","ed","eds","ibid","viz","jr","sr","capt","sgt","lt","col","gen",
    "gov","sen","rep","pres","dept","univ","assn","bros","mfg","mt","ft","lb","oz","vol","ed","trans",
};

bool isCjk(QChar c) {
    const ushort code = c.unicode();
    return (code >= 0x4E00 && code <= 0x9FFF) ||   // CJK 统一表意文字
           (code >= 0x3400 && code <= 0x4DBF) ||   // 扩展 A
           (code >= 0xF900 && code <= 0xFAFF) ||   // 兼容表意文字
           (code >= 0x3000 && code <= 0x303F) ||   // CJK 标点
           (code >= 0xFF00 && code <= 0xFFEF);     // 全角字符
}

bool isAbbreviation(const QString& lower) {
    for (const char* entry : kAbbreviations)
        if (lower == QLatin1String(entry)) return true;
    return false;
}

// 编辑距离（Levenshtein）。只用于短词，规模小，直接算完整 DP。
int editDistance(const QString& a, const QString& b) {
    if (a == b) return 0;
    if (a.isEmpty()) return b.size();
    if (b.isEmpty()) return a.size();
    QVector<int> previous(b.size() + 1);
    QVector<int> current(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j) previous[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (int j = 1; j <= b.size(); ++j) {
            const int cost = a.at(i - 1) == b.at(j - 1) ? 0 : 1;
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
        }
        previous = current;
    }
    return previous[b.size()];
}

// 是否只差一次「增/删/改」。
bool oneEditApart(const QString& a, const QString& b) {
    const int la = a.size();
    const int lb = b.size();
    if (qAbs(la - lb) > 1) return false;
    if (la == lb) {
        int differences = 0;
        for (int i = 0; i < la; ++i) {
            if (a.at(i) != b.at(i) && ++differences > 1) return false;
        }
        return differences == 1;
    }
    const QString& longer = la > lb ? a : b;
    const QString& shorter = la > lb ? b : a;
    int i = 0;
    int j = 0;
    bool skipped = false;
    while (i < longer.size() && j < shorter.size()) {
        if (longer.at(i) == shorter.at(j)) {
            ++i;
            ++j;
            continue;
        }
        if (skipped) return false;
        skipped = true;
        ++i;
    }
    return true;
}

// 是否只差一次「相邻两字换位」。手滑打错字最常见的就是这种。
bool oneTranspositionApart(const QString& a, const QString& b) {
    if (a.size() != b.size() || a.size() < 2) return false;
    int first = -1;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i) == b.at(i)) continue;
        if (first < 0) {
            first = i;
            continue;
        }
        if (i != first + 1) return false;
        if (a.at(first) != b.at(i) || a.at(i) != b.at(first)) return false;
        for (int k = i + 1; k < a.size(); ++k)
            if (a.at(k) != b.at(k)) return false;
        return true;
    }
    return false;
}

// 分词结果。
struct SpellToken {
    QString text;
    int start = 0;
    int length = 0;
    bool skip = false;             // 不参与拼写检查
    bool atSentenceStart = false;  // 是不是句首
    bool gapOnlyWhitespace = true; // 与上一个词之间是不是只有空白
};

bool containsLetter(const QString& text) {
    for (const QChar c : text)
        if (c.isLetter()) return true;
    return false;
}

}  // namespace

SpellChecker SpellChecker::builtin() {
    SpellChecker checker;
    for (const char* word : kBuiltinWords) checker.insertWord(QString::fromLatin1(word), checker.m_nextRank++);
    for (const MisspellingPair& pair : kCommonMisspellings)
        checker.m_misspellings.insert(QString::fromLatin1(pair.wrong), QString::fromLatin1(pair.right));
    return checker;
}

void SpellChecker::insertWord(const QString& word, int rank) {
    const QString lower = word.toLower();
    if (lower.isEmpty()) return;
    if (m_words.contains(lower)) return;  // 已经有这个词了：保留它更靠前的次序
    m_words.insert(lower);
    m_ranks.insert(lower, rank);
    m_byLength[lower.size()].append(lower);
}

int SpellChecker::loadUserDictionary(const QString& path, QString* error) {
    if (error) error->clear();
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("用户词典路径为空");
        return 0;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("打不开用户词典：%1").arg(file.errorString());
        return 0;
    }
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    int loaded = 0;
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        // Hunspell 的词条可以带 affix 标记（/FLAGS），这里只取词本身。
        const int flags = line.indexOf(QLatin1Char('/'));
        if (flags >= 0) line = line.left(flags).trimmed();
        if (line.isEmpty() || !containsLetter(line)) continue;  // 纯数字的行不是词
        insertWord(line, m_nextRank++);
        ++loaded;
    }
    return loaded;
}

void SpellChecker::addWord(const QString& word) {
    insertWord(word, m_nextRank++);
}

bool SpellChecker::isKnownWord(const QString& word) const {
    if (word.isEmpty()) return false;
    return m_words.contains(word.toLower());
}

bool SpellChecker::isKnownRelaxed(const QString& lower) const {
    if (m_words.contains(lower)) return true;
    if (lower.size() < 4) return false;
    // 所有格：doc's → doc
    if (lower.endsWith(QLatin1String("'s")) &&
        m_words.contains(lower.left(lower.size() - 2)))
        return true;
    // 复数：cars → car，boxes → box
    if (lower.endsWith(QLatin1Char('s'))) {
        const QString stem = lower.left(lower.size() - 1);
        if (m_words.contains(stem)) return true;
        if (stem.endsWith(QLatin1Char('e')) && m_words.contains(stem.left(stem.size() - 1))) return true;
        if (stem.endsWith(QLatin1String("ies")) &&
            m_words.contains(stem.left(stem.size() - 3) + QLatin1Char('y')))
            return true;
    }
    // 过去式与进行时：worked → work，running → run
    if (lower.endsWith(QLatin1String("ed"))) {
        const QString stem = lower.left(lower.size() - 2);
        if (m_words.contains(stem) || m_words.contains(stem + QLatin1Char('e'))) return true;
    }
    if (lower.endsWith(QLatin1String("ing"))) {
        const QString stem = lower.left(lower.size() - 3);
        if (m_words.contains(stem) || m_words.contains(stem + QLatin1Char('e'))) return true;
    }
    return false;
}

QStringList SpellChecker::collectNeighbors(const QString& word, int maxCount, bool allowTransposition) const {
    QStringList result;
    if (word.isEmpty() || maxCount <= 0) return result;
    const int length = word.size();
    for (int candidateLength = length - 1; candidateLength <= length + 1; ++candidateLength) {
        const auto bucket = m_byLength.constFind(candidateLength);
        if (bucket == m_byLength.constEnd()) continue;
        for (const QString& candidate : bucket.value()) {
            if (candidate == word) continue;
            if (!oneEditApart(word, candidate) &&
                !(allowTransposition && oneTranspositionApart(word, candidate)))
                continue;
            result.append(candidate);
        }
    }
    // 先常见后罕见：按词表里的次序排；用户词典里的词没有次序，排在最后。
    std::sort(result.begin(), result.end(), [this](const QString& a, const QString& b) {
        const int rankA = m_ranks.value(a, std::numeric_limits<int>::max());
        const int rankB = m_ranks.value(b, std::numeric_limits<int>::max());
        if (rankA != rankB) return rankA < rankB;
        return a < b;
    });
    if (result.size() > maxCount) result = result.mid(0, maxCount);
    return result;
}

QStringList SpellChecker::suggestions(const QString& word, int maxCount) const {
    if (word.isEmpty()) return {};
    // 口径就是「增/删/改一个字符」：编辑距离正好为 1。
    // 换位（teh → the）不算在内，因为那需要改两个字符。
    return collectNeighbors(word.toLower(), maxCount, false);
}

QVector<SpellIssue> SpellChecker::check(const QString& text) const {
    QVector<SpellIssue> issues;
    if (text.isEmpty()) return issues;

    // ------------------------------------------------------------ 分词 ----
    QVector<SpellToken> tokens;
    QHash<QString, int> lowerCounts;
    bool gapOnlyWhitespace = true;
    bool sawNewline = false;
    QChar lastSignificant = QChar();

    const int n = text.size();
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('\n') || c == QLatin1Char('\r')) {
            sawNewline = true;
            ++i;
            continue;
        }
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (isCjk(c)) {
            // 中文整段跳过：中文不做拼写检查，连续汉字当成分隔符。
            while (i < n && isCjk(text.at(i))) ++i;
            gapOnlyWhitespace = false;
            sawNewline = false;
            lastSignificant = c;
            continue;
        }
        if (!c.isLetter()) {
            if (c.isDigit()) {
                // 数字连着的词整块跳过（含数字的词不检查）。
                while (i < n && (text.at(i).isLetterOrNumber() || text.at(i) == QLatin1Char('_'))) ++i;
                gapOnlyWhitespace = false;
                sawNewline = false;
                lastSignificant = c;
                continue;
            }
            // 标点等：分隔符。
            gapOnlyWhitespace = false;
            sawNewline = false;
            lastSignificant = c;
            ++i;
            continue;
        }

        // 词：字母、数字、下划线、撇号连在一起的一串。
        const int start = i;
        QString word;
        bool hasDigit = false;
        bool hasUnderscore = false;
        while (i < n) {
            const QChar w = text.at(i);
            if (w.isLetter()) {
                word += w;
                ++i;
                continue;
            }
            if (w.isDigit()) {
                word += w;
                hasDigit = true;
                ++i;
                continue;
            }
            if (w == QLatin1Char('_')) {
                word += w;
                hasUnderscore = true;
                ++i;
                continue;
            }
            if (w == QLatin1Char('\'') && i + 1 < n && text.at(i + 1).isLetter()) {
                word += w;
                ++i;
                continue;
            }
            break;
        }

        // 词尾的句点：单字母词与已知缩写整体跳过（"e.g."、"etc."）。
        bool abbreviation = false;
        if (i < n && text.at(i) == QLatin1Char('.')) {
            const QString lowerWord = word.toLower();
            if (word.size() == 1 || isAbbreviation(lowerWord)) {
                abbreviation = true;
                ++i;
                if (i < n && text.at(i) == QLatin1Char('.')) ++i;  // "e.g." 的第二个点
            }
        }

        SpellToken token;
        token.text = word;
        token.start = start;
        token.length = word.size();
        token.gapOnlyWhitespace = gapOnlyWhitespace;
        token.atSentenceStart = start == 0 || sawNewline ||
                                lastSignificant == QLatin1Char('.') ||
                                lastSignificant == QLatin1Char('!') ||
                                lastSignificant == QLatin1Char('?');
        token.skip = abbreviation || hasDigit || hasUnderscore || word.size() < 3;
        tokens.append(token);
        lowerCounts[word.toLower()] += 1;

        gapOnlyWhitespace = true;
        sawNewline = false;
        // 缩写后面的那个点也算句子边界："Dr. Smith" 里的 Smith 不是大小写异常。
        lastSignificant = abbreviation ? QLatin1Char('.')
                                       : (word.isEmpty() ? lastSignificant : word.at(word.size() - 1));
    }

    // ------------------------------------------------------------ 检查 ----
    QString previousLower;
    for (const SpellToken& token : tokens) {
        const QString lower = token.text.toLower();
        if (!token.skip) {
            const bool repeated = !previousLower.isEmpty() && previousLower == lower &&
                                  token.gapOnlyWhitespace;
            if (repeated) {
                // 连续重复的单词：这是最明确的一类错误，不需要词典也知道。
                issues.append(SpellIssue{token.start, token.length, token.text, {}});
            } else if (!isKnownRelaxed(lower)) {
                const auto known = m_misspellings.constFind(lower);
                if (known != m_misspellings.constEnd()) {
                    // 常见拼错对：正确写法直接作为建议。
                    issues.append(SpellIssue{token.start, token.length, token.text, {known.value()}});
                } else {
                    // 与常用词只差一个编辑（含相邻换位）：这是「打错字」的典型形态。
                    //
                    // 为什么只对 4 个字母以上的词生效：三字母的生词离常用词太近
                    // （cat / cut、form / from），误报率明显高于收益；
                    // 三字母的常见手误由下面的常见拼错对表兜住。
                    const QStringList near = lower.size() >= 4
                                                 ? collectNeighbors(lower, 3, true)
                                                 : QStringList();
                    if (!near.isEmpty()) {
                        issues.append(SpellIssue{token.start, token.length, token.text, near});
                    } else if (!token.atSentenceStart && token.text.size() >= 4 &&
                               token.text.at(0).isUpper() && token.text.at(1).isLower() &&
                               lowerCounts.value(lower, 0) <= 1) {
                        // 句中大写、后面小写、词典里又没有：多半是把词首字母打成了大写。
                        // 同一个词在别处出现过小写形式时不报（那是专有名词，不是错误）。
                        issues.append(SpellIssue{token.start, token.length, token.text, {}});
                    }
                }
            }
        }
        previousLower = lower;
    }
    return issues;
}

}  // namespace aipdf::pdf
