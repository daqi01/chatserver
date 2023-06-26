#include"json.hpp"
using json=nlohmann::json;

#include<iostream>
#include<map>
#include<vector>

using namespace std;
 
string func1()
{
    json js;
    js["msg_type"]=2; 
    js["from"]="zhang san";
    js["to"]="li si";
    js["msg"]={{"zhangsan","hello"},{"lisi","world"}};
    js["msg_"]["lisi"]="nkk";
    vector<string> vec;
    vec.push_back("wuhan");
    vec.push_back("beijing");

    js["list"]=vec;

    return js.dump();
}

void func3()
{

}
int main()
{
    string jsbuf=func1();
    json buf=json::parse(jsbuf);

    cout<<buf["msg_type"]<<endl;

    auto arr=buf["msg_"];
    cout<<arr["lisi"]<<endl;
    auto arr2=buf["msg"];
    cout<<arr2["zhangsan"]<<endl;

    vector<string> vec=buf["list"];
    for(auto &i:vec)
    {
        cout<<i<<" ";
    }
    cout<<endl;
    return 0;
}
